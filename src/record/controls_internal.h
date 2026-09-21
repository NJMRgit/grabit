// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_CONTROLS_INTERNAL_H
#define GRABIT_RECORD_CONTROLS_INTERNAL_H

#include "wl/touch.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "region/region.h"
#include "util/util.h"

struct grabit_wl_state;
struct grabit_output;
struct wl_surface;
struct wl_callback;
struct wl_pointer;
struct wl_touch;
struct wl_cursor_theme;
struct wp_cursor_shape_device_v1;
struct wl_cursor;
struct zwlr_layer_surface_v1;

#define CB_H 50
#define CB_PAD 6
#define CB_BTN 38
#define CB_GAP 2
#define CB_SEC_GAP 10
#define CB_DOT_W 14
#define CB_TIME_W 48
#define CB_EDGE_GAP 8

/* the grab handle shown inside the region when nothing fits outside it */
#define CB_HANDLE_W 56
#define CB_HANDLE_H 12
/* the bar grows out of the handle, and the handle rises from the top edge */
#define CB_ANIM_MS 150
#define CB_RISE_MS 240

#define CB_BTN_START 0
#define CB_BTN_PAUSE 1
#define CB_BTN_STOP 2
#define CB_BTN_ABORT 3
#define CB_BTN_COUNT 4

/* one layer surface. the controls are two of these, the small handle and the
   full bar: they have separate buffers so neither is ever resized, which is
   what kwin would otherwise answer with a pointer leave */
struct ctl_output {
	struct rec_controls *st;
	struct grabit_output *go;
	bool is_handle;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct grabit_shm_pool pool;
	int32_t w;
	int32_t h;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t pixel_w;
	int32_t pixel_h;
	int32_t scale;
	bool configured;
	bool mapped;
	bool dirty;
	struct wl_callback *frame_cb;
};

struct rec_controls {
	struct grabit_wl_state *wls;
	struct ctl_output bar;
	struct ctl_output handle;

	/* no room outside the region: the handle hangs inside it and the bar opens
	   over it when the pointer reaches the handle */
	bool inside;
	bool expanded;
	double k;	 /* 0 folded, 1 open: what the open/close animation draws */
	double rise; /* 0 above the top edge, 1 in place: the handle's own rise */
	double k_from;
	double rise_from;
	int64_t k_start_ns;
	int64_t rise_start_ns;
	int32_t bx;
	int32_t by;
	int32_t bw;
	int32_t bh;
	int32_t hx;
	int32_t hy;

	atomic_int *stop_flag;
	atomic_int *pause_flag;
	atomic_int *abort_flag;
	bool paused;
	int64_t secs;

	struct wl_pointer *pointer;
	struct wl_touch *touch;
	struct gtouch_slot touch_slot;
	int32_t cx;
	int32_t cy;

	struct wp_cursor_shape_device_v1 *cursor_shape;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor_hand;
	struct wl_surface *cursor_surface;
};

/* the controls own their surface, so a surface rect is in surface-local
   coordinates and c->bx/c->by are the global position it is anchored at */
static inline struct rect ctl_surface_rect(const struct ctl_output *o) {
	return (struct rect){0, 0, o->w, o->h};
}

static inline int32_t ctl_bar_width(void) {
	return CB_PAD + CB_DOT_W + 4 + CB_TIME_W + CB_SEC_GAP +
		   CB_BTN_COUNT * CB_BTN + (CB_BTN_COUNT - 1) * CB_GAP + CB_PAD;
}

static inline bool ctl_output_visible(const struct ctl_output *o) {
	const struct rec_controls *c = o->st;
	/* the handle is up while the bar is folded away, and the bar exists while
	   it is shown or still animating */
	if (o->is_handle) return c->inside && c->k <= 0.0;
	return !c->inside || c->expanded || c->k > 0.0;
}

void ctl_btn_rect(int btn, int32_t *x, int32_t *y, int32_t *w, int32_t *h);

void ctl_apply_input_region(struct ctl_output *o);
void ctl_output_hide(struct ctl_output *o);
void ctl_set_expanded(struct rec_controls *c, bool expanded);
void ctl_output_redraw(struct ctl_output *o);
void ctl_output_request_redraw(struct ctl_output *o);
void ctl_redraw_all(struct rec_controls *c);

void ctl_input_attach(struct rec_controls *c);

#endif
