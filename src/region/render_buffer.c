// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "log.h"
#include "region/annotate.h"
#include "region/wlr_input_state.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include "region/render_internal.h"

void gren_output_configure(struct ro_output *o) {
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	/* with a fractional scale the buffer holds the panel's own pixels; without
	   one the compositor only understands whole multiples */
	bool frac = o->frac_scale > 0 && o->viewport;
	o->ratio = frac ? grabit_output_pixel_ratio(o->go) : (double)o->scale;
	if (o->ratio <= 0.0) o->ratio = 1.0;
	o->pixel_width = (int32_t)lround((double)o->width * o->ratio);
	o->pixel_height = (int32_t)lround((double)o->height * o->ratio);

	if (o->cairo_frozen_pat) {
		cairo_pattern_destroy(o->cairo_frozen_pat);
		o->cairo_frozen_pat = NULL;
	}
	if (o->cairo_frozen) {
		cairo_surface_destroy(o->cairo_frozen);
		o->cairo_frozen = NULL;
	}

	const struct image *frozen = NULL;
	if (o->st->frozen) {
		const struct image *cand = &o->st->frozen[o->idx];
		if (cand->bytes && cand->width > 0 && cand->height > 0) frozen = cand;
	}
	if (frozen) {
		cairo_format_t fmt = grabit_cairo_format_for_shm(frozen->format);
		o->cairo_frozen = grabit_cairo_image(frozen->bytes, fmt,
											 frozen->width, frozen->height,
											 frozen->stride);
		if (o->cairo_frozen) {
			o->cairo_frozen_pat = cairo_pattern_create_for_surface(o->cairo_frozen);
			double psx = frozen->width > 0
							 ? (double)o->pixel_width / (double)frozen->width
							 : 1.0;
			double psy = frozen->height > 0
							 ? (double)o->pixel_height / (double)frozen->height
							 : 1.0;
			cairo_matrix_t m;
			cairo_matrix_init_scale(&m, 1.0 / psx, 1.0 / psy);
			cairo_pattern_set_matrix(o->cairo_frozen_pat, &m);
			cairo_pattern_set_filter(o->cairo_frozen_pat, CAIRO_FILTER_GOOD);
		} else {
			cairo_surface_destroy(o->cairo_frozen);
			o->cairo_frozen = NULL;
		}
	}

	if (frac) {
		wl_surface_set_buffer_scale(o->surface, 1);
		wp_viewport_set_destination(o->viewport, o->width, o->height);
	} else {
		wl_surface_set_buffer_scale(o->surface, o->scale);
	}

	/* the frozen image is painted at buffer size on every full frame; scale it
	   once so a frame is a blit instead of a filtered pattern fill */
	if (o->frozen_cache) {
		cairo_surface_destroy(o->frozen_cache);
		o->frozen_cache = NULL;
	}
	if (o->cairo_frozen_pat) {
		cairo_surface_t *cache = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, o->pixel_width, o->pixel_height);
		if (cairo_surface_status(cache) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(cache);
		} else {
			cairo_t *cc = cairo_create(cache);
			cairo_set_operator(cc, CAIRO_OPERATOR_SOURCE);
			cairo_set_source(cc, o->cairo_frozen_pat);
			cairo_paint(cc);
			cairo_destroy(cc);
			cairo_surface_flush(cache);
			o->frozen_cache = cache;
		}
	}

	if (o->frozen_cache) {
		cairo_surface_t *dim = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, o->pixel_width, o->pixel_height);
		if (cairo_surface_status(dim) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(dim);
		} else {
			cairo_t *dc = cairo_create(dim);
			cairo_set_operator(dc, CAIRO_OPERATOR_SOURCE);
			cairo_set_source_surface(dc, o->frozen_cache, 0, 0);
			cairo_paint(dc);
			cairo_set_operator(dc, CAIRO_OPERATOR_OVER);
			cairo_set_source_rgba(dc, 0, 0, 0, REGION_DIM_A);
			cairo_paint(dc);
			cairo_destroy(dc);
			cairo_surface_flush(dim);
			o->dim_cache = dim;
		}
	}

	o->damage_full = true;
	o->damage_pending = false;
}

void region_render_free_buffer(struct ro_output *o) {
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->anno_cache) {
		cairo_surface_destroy(o->anno_cache);
		o->anno_cache = NULL;
	}
	if (o->cairo_frozen_pat) {
		cairo_pattern_destroy(o->cairo_frozen_pat);
		o->cairo_frozen_pat = NULL;
	}
	if (o->cairo_frozen) {
		cairo_surface_destroy(o->cairo_frozen);
		o->cairo_frozen = NULL;
	}
	if (o->frozen_cache) {
		cairo_surface_destroy(o->frozen_cache);
		o->frozen_cache = NULL;
	}
	if (o->dim_cache) {
		cairo_surface_destroy(o->dim_cache);
		o->dim_cache = NULL;
	}
	grabit_shm_pool_finish(&o->pool);
}
