// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/controls.h"
#include "record/controls_internal.h"

#include "cursor.h"
#include "log.h"
#include "ui_theme.h"
#include "util/util.h"
#include "wl/wl.h"
#include "wm/wm.h"

#include <stdlib.h>
#include <time.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "cursor-shape-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void ctl_apply_input_region(struct ctl_output *o) {
	struct rec_controls *c = o->st;
	struct wl_region *reg = wl_compositor_create_region(c->wls->compositor);
	if (!reg) return;
	grabit_wl_region_add_rounded(reg, 0, 0, o->w, o->h,
								 (int32_t)grabit_ui_radius(GUI_R_PANEL));
	wl_surface_set_input_region(o->surface, reg);
	wl_region_destroy(reg);
}

/* the bar and the handle are two surfaces that never resize: the hidden one is
   destroyed and built again when it comes back. detaching the buffer instead
   (wl_surface_attach NULL) makes kwin complain about a buffer attached before
   the layer surface's first configure when it is attached again later */
static void ctl_output_destroy_surface(struct ctl_output *o) {
	grabit_wl_callback_drop(&o->frame_cb);
	grabit_shm_pool_finish(&o->pool);
	if (o->layer) zwlr_layer_surface_v1_destroy(o->layer);
	if (o->surface) wl_surface_destroy(o->surface);
	o->layer = NULL;
	o->surface = NULL;
	o->configured = false;
	o->mapped = false;
	o->dirty = false;
}

void ctl_output_hide(struct ctl_output *o) {
	if (!o->surface) return;
	ctl_output_destroy_surface(o);
}

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
							uint32_t serial, uint32_t w, uint32_t h) {
	struct ctl_output *o = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	o->width = (int32_t)w;
	o->height = (int32_t)h;
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_w = o->width * o->scale;
	o->pixel_h = o->height * o->scale;

	wl_surface_set_buffer_scale(o->surface, o->scale);
	o->configured = true;
	ctl_apply_input_region(o);
	if (ctl_output_visible(o))
		ctl_output_redraw(o);
	else
		ctl_output_hide(o);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)data;
	(void)ls;
}

static const struct zwlr_layer_surface_v1_listener layer_listener_g = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static bool try_output(const struct grabit_output *o, struct rect r,
					   int32_t w, int32_t h, int32_t *bx, int32_t *by) {
	int32_t x = o->x + (o->logical_width - w) / 2;
	int32_t ys[2] = {o->y + CB_EDGE_GAP,
					 o->y + o->logical_height - CB_EDGE_GAP - h};
	for (int i = 0; i < 2; i++) {
		if (!rects_overlap((struct rect){x, ys[i], w, h}, r)) {
			*bx = x;
			*by = ys[i];
			return true;
		}
	}
	return false;
}

static bool try_place_near_region(const struct grabit_output *o, struct rect r,
								  int32_t w, int32_t h, int32_t *bx, int32_t *by) {
	int32_t x_centered = r.x + (r.w - w) / 2;
	if (x_centered < o->x + CB_EDGE_GAP) x_centered = o->x + CB_EDGE_GAP;
	if (x_centered + w > o->x + o->logical_width - CB_EDGE_GAP)
		x_centered = o->x + o->logical_width - CB_EDGE_GAP - w;

	int32_t space_above = r.y - o->y;
	int32_t space_below = o->y + o->logical_height - (r.y + r.h);

	if (space_below >= h + 2 * CB_EDGE_GAP) {
		*bx = x_centered;
		*by = r.y + r.h + CB_EDGE_GAP;
		return true;
	}
	if (space_above >= h + 2 * CB_EDGE_GAP) {
		*bx = x_centered;
		*by = r.y - h - CB_EDGE_GAP;
		return true;
	}

	int32_t y_centered = r.y + (r.h - h) / 2;
	if (y_centered < o->y + CB_EDGE_GAP) y_centered = o->y + CB_EDGE_GAP;
	if (y_centered + h > o->y + o->logical_height - CB_EDGE_GAP)
		y_centered = o->y + o->logical_height - CB_EDGE_GAP - h;

	int32_t space_left = r.x - o->x;
	int32_t space_right = o->x + o->logical_width - (r.x + r.w);

	if (space_right >= w + 2 * CB_EDGE_GAP) {
		*bx = r.x + r.w + CB_EDGE_GAP;
		*by = y_centered;
		return true;
	}
	if (space_left >= w + 2 * CB_EDGE_GAP) {
		*bx = r.x - w - CB_EDGE_GAP;
		*by = y_centered;
		return true;
	}

	return false;
}

static bool place_bar(struct grabit_wl_state *s, struct rect r,
					  int32_t w, int32_t h, int32_t *bx, int32_t *by,
					  struct grabit_output **out_go) {
	const struct grabit_output *cur = grabit_wm_active_output(s);
	if (!cur) cur = grabit_wl_output_at(s, r.x + r.w / 2, r.y + r.h / 2);
	if (!cur) cur = grabit_wl_primary_output(s);
	if (!cur) return false;

	if (try_place_near_region(cur, r, w, h, bx, by)) {
		*out_go = (struct grabit_output *)cur;
		return true;
	}
	if (try_output(cur, r, w, h, bx, by)) {
		*out_go = (struct grabit_output *)cur;
		return true;
	}

	for (size_t i = 0; i < s->n_outputs; i++) {
		const struct grabit_output *o = s->outputs[i];
		if (o == cur) continue;
		if (try_place_near_region(o, r, w, h, bx, by)) {
			*out_go = (struct grabit_output *)o;
			return true;
		}
		if (try_output(o, r, w, h, bx, by)) {
			*out_go = (struct grabit_output *)o;
			return true;
		}
	}
	return false;
}

/* the output to put the controls on: the one under the region's centre, else
   the active output, else the primary */
static const struct grabit_output *region_output(struct grabit_wl_state *s,
												 struct rect r) {
	const struct grabit_output *o =
		grabit_wl_output_at(s, r.x + r.w / 2, r.y + r.h / 2);
	if (!o) o = grabit_wm_active_output(s);
	if (!o) o = grabit_wl_primary_output(s);
	return o;
}

/* the region covers the output, so nothing fits outside it: the handle hangs
   from the top edge of the region, clamped to the output */
static bool place_inside_region(struct grabit_wl_state *s, struct rect r,
								int32_t w, int32_t h, int32_t *bx, int32_t *by,
								struct grabit_output **out_go) {
	const struct grabit_output *o = region_output(s, r);
	if (!o) return false;

	int32_t x = r.x + (r.w - w) / 2;
	int32_t y = r.y;
	int32_t xmax = o->x + o->logical_width - CB_EDGE_GAP - w;
	int32_t ymax = o->y + o->logical_height - CB_EDGE_GAP - h;
	if (x > xmax) x = xmax;
	if (x < o->x + CB_EDGE_GAP) x = o->x + CB_EDGE_GAP;
	if (y > ymax) y = ymax;
	if (y < o->y) y = o->y;

	*bx = x;
	*by = y;
	*out_go = (struct grabit_output *)o;
	return true;
}

/* the bar takes the handle's place: same top edge, centred on the handle */
static void bar_geometry(const struct grabit_output *go, int32_t hx, int32_t hy,
						 int32_t w, int32_t *bx, int32_t *by) {
	int32_t x = hx + (CB_HANDLE_W - w) / 2;
	int32_t xmax = go->x + go->logical_width - CB_EDGE_GAP - w;
	if (x > xmax) x = xmax;
	if (x < go->x + CB_EDGE_GAP) x = go->x + CB_EDGE_GAP;
	*bx = x;
	*by = hy;
}

static bool ctl_output_create_surface(struct ctl_output *o);

static void ctl_output_apply_visibility(struct ctl_output *o) {
	if (ctl_output_visible(o)) {
		if (!o->surface) {
			/* fresh surface: its configure attaches the first buffer */
			ctl_output_create_surface(o);
			return;
		}
		ctl_output_request_redraw(o);
	} else {
		ctl_output_hide(o);
	}
}

static void ctl_apply_visibility(struct rec_controls *c) {
	if (c->bar.st) ctl_output_apply_visibility(&c->bar);
	/* the handle only exists when the region left no room for the bar */
	if (c->handle.st) ctl_output_apply_visibility(&c->handle);
}

/* the surface is created without a buffer: the layer surface configure that
   follows is what lets the controls attach their first one */
static bool ctl_output_create_surface(struct ctl_output *o) {
	struct rec_controls *c = o->st;
	struct grabit_output *go = o->go;
	o->surface = wl_compositor_create_surface(c->wls->compositor);
	if (!o->surface) return false;
	o->layer = grabit_wl_layer_anchored(
		c->wls, o->surface, go->wl_output, "grabit-rec-controls",
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, o->w,
		o->h, o->y - go->y, 0, 0, o->x - go->x,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE, &layer_listener_g, o);
	if (!o->layer) {
		wl_surface_destroy(o->surface);
		o->surface = NULL;
		return false;
	}
	log_debug("record: controls %s surface %u", o->is_handle ? "handle" : "bar",
			  wl_proxy_get_id((struct wl_proxy *)o->layer));
	wl_surface_commit(o->surface);
	return true;
}

static void ctl_output_init(struct rec_controls *c, struct ctl_output *o,
							struct grabit_output *go, bool is_handle, int32_t x, int32_t y,
							int32_t w, int32_t h) {
	o->st = c;
	o->go = go;
	o->is_handle = is_handle;
	o->x = x;
	o->y = y;
	o->w = w;
	o->h = h;
}

static void ctl_attach_seat(struct rec_controls *c) {
	struct grabit_wl_state *s = c->wls;
	bool has_pointer = s->seat_caps & WL_SEAT_CAPABILITY_POINTER;
	bool has_touch = s->seat_caps & WL_SEAT_CAPABILITY_TOUCH;
	if (s->seat && !has_pointer && !has_touch) {
		log_warn("record: no pointer or touch on seat; control bar taps disabled "
				 "(stop with `grabit --record`)");
	}
	if (has_touch) c->touch = wl_seat_get_touch(s->seat);
	if (!has_pointer) return;

	c->pointer = wl_seat_get_pointer(s->seat);
	if (c->pointer && s->cursor_shape_manager) {
		c->cursor_shape = wp_cursor_shape_manager_v1_get_pointer(
			s->cursor_shape_manager, c->pointer);
	} else if (c->pointer) {
		int32_t max_scale = 1;
		for (size_t i = 0; i < s->n_outputs; i++) {
			if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
		}
		c->cursor_theme = grabit_cursor_theme_load(s->shm, max_scale);
		if (c->cursor_theme) {
			c->cursor_hand = grabit_cursor_load_hand(c->cursor_theme);
			if (c->cursor_hand)
				c->cursor_surface = wl_compositor_create_surface(s->compositor);
		}
	}
}

struct rec_controls *controls_start(struct grabit_wl_state *s, struct rect r,
									atomic_int *stop_flag, atomic_int *pause_flag,
									atomic_int *abort_flag) {
	if (!s || !s->layer_shell || !s->compositor || !s->shm || s->n_outputs == 0)
		return NULL;

	int32_t bw = ctl_bar_width();
	int32_t bx = 0, by = 0;
	int32_t hx = 0, hy = 0;
	struct grabit_output *go = NULL;
	bool inside = false;

	if (!place_bar(s, r, bw, CB_H, &bx, &by, &go) || !go) {
		/* nowhere outside the region fits the bar: fall back to a grab handle
		   hanging from the region's top edge, which opens the bar on hover */
		inside = true;
		if (!place_inside_region(s, r, CB_HANDLE_W, CB_HANDLE_H, &hx, &hy, &go) ||
			!go) {
			log_info("recording: no room for the control bar; "
					 "re-run `grabit --record` to stop");
			return NULL;
		}
		bar_geometry(go, hx, hy, bw, &bx, &by);
	}

	struct rec_controls *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	c->wls = s;
	c->inside = inside;
	c->expanded = !inside;
	c->bx = bx;
	c->by = by;
	c->bw = bw;
	c->bh = CB_H;
	c->hx = hx;
	c->hy = hy;
	c->stop_flag = stop_flag;
	c->pause_flag = pause_flag;
	c->abort_flag = abort_flag;

	ctl_output_init(c, &c->bar, go, false, bx, by, bw, CB_H);
	if (inside)
		ctl_output_init(c, &c->handle, go, true, hx, hy, CB_HANDLE_W, CB_HANDLE_H);
	ctl_apply_visibility(c);
	if (inside)
		log_info("recording: no room outside the region; control handle %dx%d at "
				 "%d,%d on %s (hover it for the bar)",
				 CB_HANDLE_W, CB_HANDLE_H, hx, hy, go->name ? go->name : "?");
	else
		log_debug("record: control bar %dx%d at %d,%d on %s", bw, CB_H, bx, by,
				  go->name ? go->name : "?");

	ctl_attach_seat(c);
	ctl_input_attach(c);
	wl_display_roundtrip(s->display);
	return c;
}

static int64_t ctl_now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static double ctl_ease(double t) {
	if (t <= 0.0) return 0.0;
	if (t >= 1.0) return 1.0;
	return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t); /* ease out */
}

static bool anim_step(double *value, double target, double *from, int64_t *start_ns,
					  double dur_ms) {
	if (*value == target && *start_ns == 0) return false;
	if (*start_ns == 0) {
		*from = *value;
		*start_ns = ctl_now_ns();
	}
	double ms = (double)(ctl_now_ns() - *start_ns) / 1e6;
	if (ms >= dur_ms) {
		*value = target;
		*start_ns = 0;
		return true;
	}
	*value = *from + (target - *from) * ctl_ease(ms / dur_ms);
	return true;
}

/* runs from the capture loop: expands the bar out of the handle, folds it
   back, and lets the handle rise out of the top edge when it is alone */
static void ctl_anim_step(struct rec_controls *c) {
	if (!c->inside) return;
	bool busy = false;

	if (c->expanded && !c->bar.surface && c->k <= 0.0) {
		ctl_output_create_surface(&c->bar);
		busy = true;
	}
	if (c->bar.surface &&
		anim_step(&c->k, c->expanded ? 1.0 : 0.0, &c->k_from, &c->k_start_ns,
				  CB_ANIM_MS))
		busy = true;
	if (!c->expanded && c->k <= 0.0 && c->bar.surface) {
		ctl_output_hide(&c->bar);
		busy = true;
	}

	if (!c->expanded && c->k <= 0.0) {
		if (!c->handle.surface) {
			ctl_output_create_surface(&c->handle);
			busy = true;
		}
		if (anim_step(&c->rise, 1.0, &c->rise_from, &c->rise_start_ns, CB_RISE_MS))
			busy = true;
	} else if (c->rise > 0.0) {
		if (anim_step(&c->rise, 0.0, &c->rise_from, &c->rise_start_ns, CB_RISE_MS))
			busy = true;
		if (c->rise <= 0.0 && c->handle.surface) {
			ctl_output_hide(&c->handle);
			busy = true;
		}
	}
	if (busy) ctl_redraw_all(c);
}

void ctl_set_expanded(struct rec_controls *c, bool expanded) {
	if (!c || !c->inside || c->expanded == expanded) return;
	c->expanded = expanded;
	log_debug("record: controls %s", expanded ? "opened" : "closed");
	if (expanded && !c->bar.surface) ctl_output_create_surface(&c->bar);
}

void controls_set_paused(struct rec_controls *c, bool paused) {
	if (!c || c->paused == paused) return;
	c->paused = paused;
	ctl_redraw_all(c);
}

void controls_tick(struct rec_controls *c, int64_t secs) {
	if (!c) return;
	ctl_anim_step(c);
	if (c->secs == secs) return;
	c->secs = secs;
	ctl_redraw_all(c);
}

void ctl_redraw_all(struct rec_controls *c) {
	if (!c) return;
	if (c->bar.surface && ctl_output_visible(&c->bar)) {
		ctl_output_request_redraw(&c->bar);
		return;
	}
	if (c->handle.surface && ctl_output_visible(&c->handle))
		ctl_output_request_redraw(&c->handle);
}

void controls_stop(struct rec_controls *c) {
	if (!c) return;
	if (c->pointer) wl_pointer_release(c->pointer);
	if (c->touch) wl_touch_release(c->touch);
	if (c->cursor_shape) wp_cursor_shape_device_v1_destroy(c->cursor_shape);
	if (c->cursor_surface) wl_surface_destroy(c->cursor_surface);
	if (c->cursor_theme) wl_cursor_theme_destroy(c->cursor_theme);
	ctl_output_destroy_surface(&c->bar);
	ctl_output_destroy_surface(&c->handle);
	if (c->wls && c->wls->display) wl_display_roundtrip(c->wls->display);
	free(c);
}
