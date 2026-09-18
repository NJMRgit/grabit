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

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "cursor-shape-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void ctl_apply_input_region(struct ctl_output *o) {
	struct rec_controls *c = o->st;
	struct wl_region *reg = wl_compositor_create_region(c->wls->compositor);
	if (!reg) return;
	grabit_wl_region_add_rounded(reg, 0, 0, c->bw, c->bh,
								 (int32_t)grabit_ui_radius(GUI_R_PANEL));
	wl_surface_set_input_region(o->surface, reg);
	wl_region_destroy(reg);
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
	o->mapped = false;
	ctl_apply_input_region(o);
	ctl_output_redraw(o);
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

struct rec_controls *controls_start(struct grabit_wl_state *s, struct rect r,
									atomic_int *stop_flag, atomic_int *pause_flag,
									atomic_int *abort_flag) {
	if (!s || !s->layer_shell || !s->compositor || !s->shm || s->n_outputs == 0)
		return NULL;

	int32_t w = ctl_bar_width(), h = CB_H;
	int32_t bx = 0, by = 0;
	struct grabit_output *go = NULL;
	if (!place_bar(s, r, w, h, &bx, &by, &go) || !go) {
		log_info("recording: no room for the control bar outside the region; "
				 "re-run `grabit --record` to stop");
		return NULL;
	}

	struct rec_controls *c = calloc(1, sizeof *c);
	if (!c) return NULL;
	c->wls = s;
	c->bw = w;
	c->bh = h;
	c->bx = bx;
	c->by = by;
	c->stop_flag = stop_flag;
	c->pause_flag = pause_flag;
	c->abort_flag = abort_flag;

	struct ctl_output *o = &c->out;
	o->st = c;
	o->go = go;
	o->surface = wl_compositor_create_surface(s->compositor);
	if (!o->surface) {
		free(c);
		return NULL;
	}
	o->layer = grabit_wl_layer_anchored(
		s, o->surface, go->wl_output, "grabit-rec-controls",
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT, w, h,
		by - go->y, 0, 0, bx - go->x,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE, &layer_listener_g, o);
	if (!o->layer) {
		wl_surface_destroy(o->surface);
		free(c);
		return NULL;
	}
	c->have_out = true;
	wl_surface_commit(o->surface);

	bool has_pointer = s->seat_caps & WL_SEAT_CAPABILITY_POINTER;
	bool has_touch = s->seat_caps & WL_SEAT_CAPABILITY_TOUCH;
	if (s->seat && !has_pointer && !has_touch) {
		log_warn("record: no pointer or touch on seat; control bar taps disabled "
				 "(stop with `grabit --record`)");
	}
	if (has_touch) c->touch = wl_seat_get_touch(s->seat);
	if (has_pointer) {
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
	ctl_input_attach(c);

	wl_display_roundtrip(s->display);
	return c;
}

void controls_set_paused(struct rec_controls *c, bool paused) {
	if (!c || c->paused == paused) return;
	c->paused = paused;
	ctl_redraw_all(c);
}

void controls_tick(struct rec_controls *c, int64_t secs) {
	if (!c || c->secs == secs) return;
	c->secs = secs;
	ctl_redraw_all(c);
}

void controls_stop(struct rec_controls *c) {
	if (!c) return;
	if (c->pointer) wl_pointer_release(c->pointer);
	if (c->touch) wl_touch_release(c->touch);
	if (c->cursor_shape) wp_cursor_shape_device_v1_destroy(c->cursor_shape);
	if (c->cursor_surface) wl_surface_destroy(c->cursor_surface);
	if (c->cursor_theme) wl_cursor_theme_destroy(c->cursor_theme);
	struct ctl_output *o = &c->out;
	grabit_wl_callback_drop(&o->frame_cb);
	grabit_shm_pool_finish(&o->pool);
	if (o->layer) zwlr_layer_surface_v1_destroy(o->layer);
	if (o->surface) wl_surface_destroy(o->surface);
	if (c->wls && c->wls->display) wl_display_roundtrip(c->wls->display);
	free(c);
}
