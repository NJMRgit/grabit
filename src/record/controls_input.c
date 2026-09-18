// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/controls_internal.h"

#include "cursor.h"
#include "wl/wl.h"

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "cursor-shape-v1-client-protocol.h"

static int btn_at(int32_t x, int32_t y) {
	for (int b = 0; b < CB_BTN_COUNT; b++) {
		int32_t bx, by, bw, bh;
		ctl_btn_rect(b, &bx, &by, &bw, &bh);
		if (rect_contains((struct rect){bx, by, bw, bh}, x, y)) return b;
	}
	return -1;
}

static bool enter_output(struct rec_controls *c, struct wl_surface *surface,
						 wl_fixed_t sx, wl_fixed_t sy) {
	if (!c->have_out || c->out.surface != surface) return false;
	c->cx = wl_fixed_to_int(sx);
	c->cy = wl_fixed_to_int(sy);
	return true;
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct rec_controls *c = data;
	if (!enter_output(c, surface, sx, sy)) return;
	if (c->cursor_shape)
		wp_cursor_shape_device_v1_set_shape(c->cursor_shape, serial,
											WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
	else
		grabit_cursor_apply(p, serial, c->cursor_surface, c->cursor_hand,
							c->out.scale > 0 ? c->out.scale : 1);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)data;
	(void)p;
	(void)serial;
	(void)surface;
}

static void motion_event(struct rec_controls *c, wl_fixed_t sx, wl_fixed_t sy) {
	c->cx = wl_fixed_to_int(sx);
	c->cy = wl_fixed_to_int(sy);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	motion_event(data, sx, sy);
}

static void press_event(struct rec_controls *c) {
	if (!rect_contains(ctl_bar_rect(c), c->cx, c->cy)) return;
	switch (btn_at(c->cx, c->cy)) {
	case CB_BTN_START:
		atomic_store(c->pause_flag, 0);
		break;
	case CB_BTN_PAUSE:
		atomic_store(c->pause_flag, 1);
		break;
	case CB_BTN_STOP:
		atomic_store(c->stop_flag, 1);
		break;
	case CB_BTN_ABORT:
		atomic_store(c->abort_flag, 1);
		atomic_store(c->stop_flag, 1);
		break;
	default:
		break;
	}
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	(void)time;
	if (button != BTN_LEFT) return;
	if (state == WL_POINTER_BUTTON_STATE_RELEASED) return;
	press_event(data);
}

static void touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
					   struct wl_surface *surface, int32_t id, wl_fixed_t sx,
					   wl_fixed_t sy) {
	(void)t;
	(void)serial;
	(void)time;
	struct rec_controls *c = data;
	if (!gtouch_claim(&c->touch_slot, id)) return;
	if (!enter_output(c, surface, sx, sy)) {
		gtouch_clear(&c->touch_slot);
		return;
	}
	press_event(c);
}

static void touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
					 int32_t id) {
	(void)t;
	(void)serial;
	(void)time;
	struct rec_controls *c = data;
	gtouch_release(&c->touch_slot, id);
}

static void touch_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id,
						 wl_fixed_t sx, wl_fixed_t sy) {
	(void)t;
	(void)time;
	struct rec_controls *c = data;
	if (!gtouch_owns(&c->touch_slot, id)) return;
	motion_event(c, sx, sy);
}

static void touch_cancel(void *data, struct wl_touch *t) {
	(void)t;
	struct rec_controls *c = data;
	gtouch_cancel(&c->touch_slot);
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

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};

void ctl_input_attach(struct rec_controls *c) {
	if (c->pointer) wl_pointer_add_listener(c->pointer, &pointer_listener_g, c);
	if (c->touch) wl_touch_add_listener(c->touch, &touch_listener_g, c);
}
