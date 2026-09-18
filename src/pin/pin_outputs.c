// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "log.h"
#include "wl/wl.h"

#include <stdlib.h>

#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* A pin owns one layer surface per output, each anchored at the part of the pin
   that lies on that output and sized to it. A surface covering the whole output
   makes the compositor draw its backdrop effects behind the surface (kwin blurs
   behind translucent layer surfaces), which would blur or tint everything the
   pin overlaps. */

static void pin_output_destroy(struct pin_output *o) {
	pin_render_output_free(o);
	if (o->fractional) wp_fractional_scale_v1_destroy(o->fractional);
	if (o->viewport) wp_viewport_destroy(o->viewport);
	if (o->layer) zwlr_layer_surface_v1_destroy(o->layer);
	if (o->surface) wl_surface_destroy(o->surface);
	free(o);
}

static bool pin_overlap(const struct pin_state *st, const struct grabit_output *go,
						struct rect *out) {
	if (st->drag_full) {
		*out = (struct rect){go->x, go->y, go->logical_width, go->logical_height};
		return out->w > 0 && out->h > 0;
	}
	struct rect pr = pin_rect(st);
	return grabit_output_rect_intersect(go, &pr, &out->x, &out->y, &out->w, &out->h);
}

static void pin_output_place(struct pin_output *o, struct rect vis) {
	if (rect_equal(o->vis, vis)) return;
	bool resized = o->vis.w != vis.w || o->vis.h != vis.h;
	o->vis = vis;
	log_debug("pin: surface %dx%d at %d,%d on %s", vis.w, vis.h, vis.x,
			  vis.y, o->go->name ? o->go->name : "?");
	if (!o->layer) return;
	zwlr_layer_surface_v1_set_size(o->layer, (uint32_t)vis.w, (uint32_t)vis.h);
	zwlr_layer_surface_v1_set_margin(o->layer, vis.y - o->go->y, 0, 0,
									 vis.x - o->go->x);
	/* a new configure carries the size the next buffer must have */
	if (resized) o->configured = false;
	wl_surface_commit(o->surface);
}

static int pin_output_create(struct pin_state *st, struct grabit_output *go,
							 struct rect vis) {
	struct pin_output **p = realloc(st->outs, (st->n + 1) * sizeof *p);
	if (!p) return -1;
	st->outs = p;
	struct pin_output *o = calloc(1, sizeof *o);
	if (!o) return -1;
	o->st = st;
	o->go = go;
	o->vis = vis;
	o->width = vis.w;
	o->height = vis.h;
	o->scale = go->scale > 0 ? go->scale : 1;
	if (o->scale > st->cursor_scale) st->cursor_scale = o->scale;
	o->surface = wl_compositor_create_surface(st->wls->compositor);
	if (!o->surface || pin_render_create_layer(o) != 0) {
		if (o->surface) wl_surface_destroy(o->surface);
		free(o);
		return -1;
	}
	pin_render_create_fractional(o);
	log_debug("pin: surface %dx%d at %d,%d on %s", vis.w, vis.h, vis.x, vis.y,
			  go->name ? go->name : "?");
	grabit_wl_clear_input_region(st->wls->compositor, o->surface);
	wl_surface_commit(o->surface);
	st->outs[st->n++] = o;
	return 0;
}

static bool pin_has_output(const struct pin_state *st,
						   const struct grabit_output *go) {
	for (size_t i = 0; i < st->n; i++)
		if (st->outs[i]->go == go) return true;
	return false;
}

void pin_sync_outputs(struct pin_state *st) {
	struct grabit_wl_state *s = st->wls;

	grabit_wl_outputs_bbox(s, &st->bounds);
	if (st->bounds.w > 0 && st->bounds.h > 0) {
		struct rect r = rect_clamp_into(pin_rect(st), st->bounds);
		st->px = r.x;
		st->py = r.y;
	}

	for (size_t i = 0; i < st->n;) {
		struct pin_output *o = st->outs[i];
		struct rect vis;
		if (o->go->dead || (st->transient && o->go != st->target) ||
			!pin_overlap(st, o->go, &vis)) {
			if (st->ptr_on == o) st->ptr_on = NULL;
			pin_output_destroy(o);
			st->outs[i] = st->outs[--st->n];
			continue;
		}
		pin_output_place(o, vis);
		i++;
	}

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *go = s->outputs[i];
		if (go->dead || go->logical_width <= 0) continue;
		if (st->transient && go != st->target) continue;
		struct rect vis;
		if (!pin_overlap(st, go, &vis)) continue;
		if (!pin_has_output(st, go)) pin_output_create(st, go, vis);
	}

	if (st->n == 0) {
		st->finished = true;
		return;
	}
	pin_render_redraw_all(st);
}

void pin_outputs_finish(struct pin_state *st) {
	for (size_t i = 0; i < st->n; i++)
		pin_output_destroy(st->outs[i]);
	free(st->outs);
	st->outs = NULL;
	st->n = 0;
}
