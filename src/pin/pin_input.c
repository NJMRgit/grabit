// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "log.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "relative-pointer-unstable-v1-client-protocol.h"

void pin_input_apply_region(struct pin_output *o) {
	struct pin_state *st = o->st;
	if (!st->wls->compositor || !o->surface) return;
	struct wl_region *reg = wl_compositor_create_region(st->wls->compositor);
	if (!reg) return;
	struct rect want = {0, 0, 0, 0};
	if (!st->transient || st->clickable || st->input_grabbed)
		want = (struct rect){0, 0, o->width, o->height};
	if (want.x == o->region.x && want.y == o->region.y &&
		want.w == o->region.w && want.h == o->region.h) {
		wl_region_destroy(reg);
		return;
	}
	int32_t corner = 0;
	if (st->transient && st->img_w > 0)
		corner = (int32_t)lround(grabit_ui_radius(GUI_R_PANEL) *
								 (double)st->width / (double)st->img_w);
	if (want.w > 0)
		grabit_wl_region_add_rounded(reg, want.x, want.y, want.w, want.h, corner);
	wl_surface_set_input_region(o->surface, reg);
	wl_region_destroy(reg);
	o->region = want;
}

static void pin_move_to(struct pin_state *st, int32_t x, int32_t y) {
	struct rect r = rect_clamp_into((struct rect){x, y, st->width, st->height},
									st->bounds);
	x = r.x;
	y = r.y;
	if (x == st->px && y == st->py) return;
	st->px = x;
	st->py = y;
	pin_sync_outputs(st);
}

/* Deltas from zwp_relative_pointer_v1: they describe the pointer alone, so the
   pin can be dragged without any of it leaking back through surface
   coordinates. Throttled so a 1 kHz mouse does not redraw every event. */
void pin_drag_apply(struct pin_state *st) {
	int32_t mx = (int32_t)st->drag_acc_x;
	int32_t my = (int32_t)st->drag_acc_y;
	if (mx == 0 && my == 0) return;
	uint64_t now = grabit_now_ns();
	if (st->drag_last_ns && now - st->drag_last_ns < 8000000ULL) return;
	st->drag_last_ns = now;
	st->drag_acc_x -= mx;
	st->drag_acc_y -= my;
	pin_move_to(st, st->px + mx, st->py + my);
}

static void relative_motion(void *data, struct zwp_relative_pointer_v1 *rp,
							uint32_t utime_hi, uint32_t utime_lo, wl_fixed_t dx,
							wl_fixed_t dy, wl_fixed_t dx_unaccel,
							wl_fixed_t dy_unaccel) {
	(void)rp;
	(void)utime_hi;
	(void)utime_lo;
	(void)dx_unaccel;
	(void)dy_unaccel;
	struct pin_state *st = data;
	if (!st->dragging) return;
	st->drag_acc_x += wl_fixed_to_double(dx);
	st->drag_acc_y += wl_fixed_to_double(dy);
	pin_drag_apply(st);
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener_g = {
	.relative_motion = relative_motion,
};

static void pin_relative_pointer_init(struct pin_state *st) {
	if (!st->pointer || !st->wls->relative_pointer_manager) return;
	st->rel_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
		st->wls->relative_pointer_manager, st->pointer);
	if (!st->rel_pointer) return;
	zwp_relative_pointer_v1_add_listener(st->rel_pointer,
										 &relative_pointer_listener_g, st);
}

static struct pin_output *output_for_surface(struct pin_state *st,
											 struct wl_surface *surface) {
	for (size_t i = 0; i < st->n; i++) {
		if (st->outs[i]->surface == surface) return st->outs[i];
	}
	return NULL;
}

static void pin_dismiss_rearm(struct pin_state *st) {
	if (!st->transient || st->dismiss_timer_fd < 0 || st->dismiss_secs <= 0) return;
	struct itimerspec it = {.it_value = {.tv_sec = st->dismiss_secs}};
	timerfd_settime(st->dismiss_timer_fd, 0, &it, NULL);
}

static bool enter_output(struct pin_state *st, struct wl_surface *surface,
						 wl_fixed_t sx, wl_fixed_t sy) {
	struct pin_output *o = output_for_surface(st, surface);
	if (!o) return false;
	st->ptr_on = o;
	st->cx = o->vis.x + wl_fixed_to_int(sx);
	st->cy = o->vis.y + wl_fixed_to_int(sy);
	return true;
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	struct pin_state *st = data;
	if (!enter_output(st, surface, sx, sy)) return;
	st->last_pointer_serial = serial;
	if (st->hover_caption && !st->hover_active) {
		st->hover_active = true;
		pin_render_redraw_all(st);
	}
	if (!st->hovering) {
		st->hovering = true;
		pin_render_redraw_all(st);
	}
	pin_dismiss_rearm(st);
	pin_cursor_refresh(st);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	struct pin_state *st = data;
	if (st->ptr_on && st->ptr_on->surface != surface) return;
	st->ptr_on = NULL;
	if (st->hovering) {
		st->hovering = false;
		pin_render_redraw_all(st);
	}
	if (st->hover_caption && st->hover_active) {
		st->hover_active = false;
		pin_render_redraw_all(st);
	}
	st->cursor_kind = PIN_CUR_NONE;
}

static void motion_event(struct pin_state *st, wl_fixed_t sx, wl_fixed_t sy) {
	if (!st->ptr_on) return;
	st->cx = st->ptr_on->vis.x + wl_fixed_to_int(sx);
	st->cy = st->ptr_on->vis.y + wl_fixed_to_int(sy);
	pin_cursor_update(st);
}

static void release_event(struct pin_state *st) {
	if (st->dragging) {
		st->drag_last_ns = 0;
		pin_drag_apply(st);
	}
	st->dragging = false;
	if (st->drag_full) {
		st->drag_full = false;
		pin_sync_outputs(st);
	}
	pin_cursor_update(st);
}

static void press_event(struct pin_state *st) {
	if (!st->ptr_on || !rect_contains(pin_rect(st), st->cx, st->cy)) return;

	if (st->clickable) {
		if (st->click_open && st->click_open[0]) {
			pid_t cpid = fork();
			if (cpid < 0) {
				log_warn("pin: fork for xdg-open failed (%s)", strerror(errno));
			} else if (cpid == 0) {
				setsid();
				execlp("xdg-open", "xdg-open", st->click_open, (char *)NULL);
				_exit(127);
			}
		}
		st->finished = true;
		return;
	}
	if (pin_in_close_button(st)) {
		st->finished = true;
		return;
	}
	if (st->transient && !st->input_grabbed) return;

	st->dragging = true;
	st->grab_dx = st->cx - st->px;
	st->grab_dy = st->cy - st->py;
	st->drag_acc_x = 0;
	st->drag_acc_y = 0;
	st->drag_last_ns = 0;
	/* grow first, then only the image moves for the rest of the drag */
	st->drag_full = true;
	pin_sync_outputs(st);
	pin_cursor_update(st);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	motion_event(data, sx, sy);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)time;
	struct pin_state *st = data;
	st->last_pointer_serial = serial;
	if (button != BTN_LEFT) return;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED)
		release_event(st);
	else
		press_event(st);
}

static void touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
					   struct wl_surface *surface, int32_t id, wl_fixed_t sx,
					   wl_fixed_t sy) {
	(void)t;
	(void)serial;
	(void)time;
	struct pin_state *st = data;
	if (!gtouch_claim(&st->touch_slot, id)) return;
	if (!enter_output(st, surface, sx, sy)) {
		gtouch_clear(&st->touch_slot);
		return;
	}
	pin_dismiss_rearm(st);
	press_event(st);
}

static void touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
					 int32_t id) {
	(void)t;
	(void)serial;
	(void)time;
	struct pin_state *st = data;
	if (!gtouch_release(&st->touch_slot, id)) return;
	release_event(st);
}

static void touch_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id,
						 wl_fixed_t sx, wl_fixed_t sy) {
	(void)t;
	(void)time;
	struct pin_state *st = data;
	if (!gtouch_owns(&st->touch_slot, id)) return;
	motion_event(st, sx, sy);
}

static void touch_cancel(void *data, struct wl_touch *t) {
	(void)t;
	struct pin_state *st = data;
	if (!gtouch_cancel(&st->touch_slot)) return;
	release_event(st);
}

static const struct wl_touch_listener touch_listener_g = {
	.down = touch_down,
	.up = touch_up,
	.motion = touch_motion,
	.frame = gtouch_frame_noop,
	.cancel = touch_cancel,
};

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}
static void pointer_frame(void *data, struct wl_pointer *p) {
	(void)data;
	(void)p;
}
static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {
	(void)data;
	(void)p;
	(void)source;
}
static void pointer_axis_stop(void *data, struct wl_pointer *p,
							  uint32_t time, uint32_t axis) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
}
static void pointer_axis_discrete(void *data, struct wl_pointer *p,
								  uint32_t axis, int32_t discrete) {
	(void)data;
	(void)p;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

void pin_input_attach(struct pin_state *st) {
	bool has_pointer = st->wls->seat_caps & WL_SEAT_CAPABILITY_POINTER;
	bool has_touch = st->wls->seat_caps & WL_SEAT_CAPABILITY_TOUCH;
	if (!has_pointer && !has_touch) {
		log_warn("pin: no pointer or touch on seat; click-to-close disabled "
				 "(dismiss with `grabit --close-all`)");
		return;
	}
	if (has_pointer) st->pointer = wl_seat_get_pointer(st->wls->seat);
	if (has_touch) st->touch = wl_seat_get_touch(st->wls->seat);
	if (st->pointer) wl_pointer_add_listener(st->pointer, &pointer_listener_g, st);
	pin_relative_pointer_init(st);
	if (st->touch) wl_touch_add_listener(st->touch, &touch_listener_g, st);
}
