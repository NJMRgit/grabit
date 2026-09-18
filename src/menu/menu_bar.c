// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "menu/menu_bar.h"

#include "cairo_util.h"
#include "config/config.h"
#include "log.h"
#include "ui_theme.h"
#include "util/util.h"
#include "wl/wl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>

static struct menu_bar *g_current;

void menu_bar_set_current(struct menu_bar *mb) {
	g_current = mb;
}

struct menu_bar *menu_bar_current(void) {
	return g_current;
}

bool menu_action_uses_edit(enum menu_action a) {
	return a == MA_COPY || a == MA_SAVE || a == MA_UPLOAD || a == MA_PIN;
}

const char *menu_action_name(enum menu_action a) {
	switch (a) {
	case MA_COPY:
		return "Copy";
	case MA_SAVE:
		return "Save";
	case MA_OCR:
		return "OCR";
	case MA_TRANSLATE:
		return "Translate";
	case MA_UPLOAD:
		return "Upload";
	case MA_PIN:
		return "Pin";
	default:
		return "Record";
	}
}

static int32_t text_width(const char *s, double size, bool bold) {
	static cairo_surface_t *surf;
	if (!surf) surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	if (!surf) return 0;
	cairo_t *cr = cairo_create(surf);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
						   bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, size);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, s, &ext);
	cairo_destroy(cr);
	return (int32_t)ceil(ext.width) + 1;
}

void menu_bar_init(struct menu_bar *mb, struct config *cfg) {
	memset(mb, 0, sizeof *mb);
	mb->act = MA_COPY;
	mb->hover = -1;
	const char *name = config_get(cfg, "default_action");
	if (name) {
		if (strcmp(name, "upload") == 0)
			mb->act = MA_UPLOAD;
		else if (strcmp(name, "save") == 0)
			mb->act = MA_SAVE;
		else if (strcmp(name, "pin") == 0)
			mb->act = MA_PIN;
	}
}

void menu_bar_place(struct menu_bar *mb, const struct grabit_output *go) {
	mb->n = 0;
	int32_t avail = go ? go->logical_width - 2 * MENU_EDGE_GAP : 0;
	int32_t pad = MENU_CHIP_PAD;

	for (int pass = 0; pass < 2; pass++) {
		int32_t w = MENU_BAR_PAD * 2;
		mb->n = 0;
		for (int i = 0; i < MENU_CHIP_COUNT; i++) {
			const char *label = i == MENU_OP_ANNOTATE ? "Annotate" : menu_action_name((enum menu_action)i);
			int32_t cw = pad * 2 + MENU_ICON + MENU_ICON_GAP +
						 text_width(label, MENU_LABEL_SIZE, false);
			mb->chips[mb->n] = (struct menu_chip){
				.r = {w, MENU_BAR_PAD, cw, MENU_BAR_H - MENU_BAR_PAD * 2},
				.id = i,
				.opt = i == MENU_OP_ANNOTATE,
			};
			mb->n++;
			w += cw;
			if (i + 1 < MENU_CHIP_COUNT) w += MENU_GAP;
		}
		w += MENU_BAR_PAD;
		if (avail <= 0 || w <= avail) {
			mb->w = w;
			break;
		}
		pad = 6;
	}

	mb->h = MENU_BAR_H;
	int32_t x = go->x + (go->logical_width - mb->w) / 2;
	int32_t y = go->y + go->logical_height - MENU_BAR_H - MENU_DOCK_GAP;
	if (x < go->x + MENU_EDGE_GAP) x = go->x + MENU_EDGE_GAP;
	if (y < go->y + MENU_EDGE_GAP) y = go->y + MENU_EDGE_GAP;
	mb->go = go;
	mb->bar = (struct rect){x, y, mb->w, MENU_BAR_H};
	mb->placed = true;
	for (int i = 0; i < mb->n; i++) {
		struct menu_chip *c = &mb->chips[i];
		log_debug("menu: chip %d (%s) at %d,%d %dx%d", i,
				  c->opt ? "Annotate" : menu_action_name((enum menu_action)c->id),
				  mb->bar.x + c->r.x, mb->bar.y + c->r.y, c->r.w, c->r.h);
	}
}

bool menu_bar_hover(struct menu_bar *mb, int32_t x, int32_t y) {
	int hover = -1;
	if (rect_contains(mb->bar, x, y)) {
		for (int i = 0; i < mb->n; i++) {
			struct rect r = mb->chips[i].r;
			r.x += mb->bar.x;
			r.y += mb->bar.y;
			if (rect_contains(r, x, y)) {
				hover = i;
				break;
			}
		}
	}
	if (hover == mb->hover) return false;
	mb->hover = hover;
	return true;
}

bool menu_bar_press(struct menu_bar *mb, int32_t x, int32_t y) {
	if (!rect_contains(mb->bar, x, y)) return false;
	log_debug("menu: bar click at %d,%d", x, y);
	for (int i = 0; i < mb->n; i++) {
		struct rect r = mb->chips[i].r;
		r.x += mb->bar.x;
		r.y += mb->bar.y;
		if (!rect_contains(r, x, y)) continue;
		struct menu_chip *c = &mb->chips[i];
		log_debug("menu: bar chip %d (%s) %s", i,
				  c->opt ? "Annotate" : menu_action_name((enum menu_action)c->id),
				  c->opt ? "toggled" : "selected");
		if (c->opt)
			mb->annotate = !mb->annotate;
		else {
			mb->act = (enum menu_action)c->id;
			if (!menu_action_uses_edit(mb->act)) mb->annotate = false;
		}
		return true;
	}
	return true; /* click on the bar, but between chips: swallow it */
}

/* ---- glyphs (drawn centred on cx,cy, s is roughly half the box size) ---- */

static void glyph_copy(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.5);
	grabit_cairo_rect_r(cr, cx - s * 0.9, cy - s * 0.9, s * 1.2, s * 1.2, 1.8);
	cairo_stroke(cr);
	grabit_cairo_rect_r(cr, cx - s * 0.3, cy - s * 0.3, s * 1.2, s * 1.2, 1.8);
	cairo_stroke(cr);
}

static void glyph_save(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.7);
	cairo_move_to(cr, cx, cy - s);
	cairo_line_to(cr, cx, cy + s * 0.35);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s * 0.42, cy - s * 0.05);
	cairo_line_to(cr, cx, cy + s * 0.45);
	cairo_line_to(cr, cx + s * 0.42, cy - s * 0.05);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s, cy + s * 0.9);
	cairo_line_to(cr, cx + s, cy + s * 0.9);
	cairo_stroke(cr);
}

static void glyph_ocr(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.5);
	grabit_cairo_rect_r(cr, cx - s, cy - s * 0.85, s * 2.0, s * 1.7, 1.8);
	cairo_stroke(cr);
	for (int i = 0; i < 3; i++) {
		double y = cy - s * 0.32 + i * s * 0.38;
		cairo_move_to(cr, cx - s * 0.6, y);
		cairo_line_to(cr, cx + (i == 1 ? s * 0.25 : s * 0.6), y);
	}
	cairo_stroke(cr);
}

static void glyph_translate(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.5);
	grabit_cairo_rect_r(cr, cx - s, cy - s * 0.9, s * 1.45, s * 1.15, 1.8);
	cairo_stroke(cr);
	grabit_cairo_rect_r(cr, cx - s * 0.35, cy - s * 0.05, s * 1.45, s * 1.15, 1.8);
	cairo_stroke(cr);
}

static void glyph_upload(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.7);
	cairo_move_to(cr, cx, cy + s);
	cairo_line_to(cr, cx, cy - s * 0.4);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s * 0.42, cy - s * 0.05);
	cairo_line_to(cr, cx, cy - s * 0.55);
	cairo_line_to(cr, cx + s * 0.42, cy - s * 0.05);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s, cy - s * 0.9);
	cairo_line_to(cr, cx + s, cy - s * 0.9);
	cairo_stroke(cr);
}

static void glyph_pin(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.7);
	cairo_arc(cr, cx, cy - s * 0.35, s * 0.58, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s * 0.4, cy + s * 0.1);
	cairo_line_to(cr, cx, cy + s);
	cairo_line_to(cr, cx + s * 0.4, cy + s * 0.1);
	cairo_stroke(cr);
}

static void glyph_record(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_source_rgba(cr, 0.90, 0.25, 0.22, 1.0);
	cairo_arc(cr, cx, cy, s * 0.95, 0, 2 * M_PI);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
	cairo_arc(cr, cx, cy, s * 0.40, 0, 2 * M_PI);
	cairo_fill(cr);
}

static void glyph_pen(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.6);
	cairo_move_to(cr, cx - s * 0.85, cy + s * 0.85);
	cairo_line_to(cr, cx + s * 0.7, cy - s * 0.7);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s * 0.95, cy + s * 0.95);
	cairo_line_to(cr, cx - s * 0.25, cy + s * 0.55);
	cairo_line_to(cr, cx - s * 0.6, cy + s * 0.2);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void glyph_draw(cairo_t *cr, const struct menu_chip *c, double cx, double cy,
					   double s, double alpha) {
	cairo_set_source_rgba(cr, 1, 1, 1, alpha);
	switch (c->id) {
	case MA_COPY:
		glyph_copy(cr, cx, cy, s);
		break;
	case MA_SAVE:
		glyph_save(cr, cx, cy, s);
		break;
	case MA_OCR:
		glyph_ocr(cr, cx, cy, s);
		break;
	case MA_TRANSLATE:
		glyph_translate(cr, cx, cy, s);
		break;
	case MA_UPLOAD:
		glyph_upload(cr, cx, cy, s);
		break;
	case MA_PIN:
		glyph_pin(cr, cx, cy, s);
		break;
	case MA_RECORD:
		glyph_record(cr, cx, cy, s);
		break;
	default:
		glyph_pen(cr, cx, cy, s);
		break;
	}
}

void menu_bar_render(cairo_t *cr, struct menu_bar *mb, const struct grabit_output *go,
					 double scale) {
	(void)scale;
	if (!mb->placed || go != mb->go) return;

	cairo_save(cr);
	cairo_translate(cr, mb->bar.x - go->x, mb->bar.y - go->y);
	grabit_ui_panel(cr, 0, 0, mb->bar.w, mb->bar.h, 1.0);

	for (int i = 0; i < mb->n; i++) {
		const struct menu_chip *c = &mb->chips[i];
		bool selected = c->opt ? mb->annotate : (int)mb->act == c->id;
		bool hovered = mb->hover == i;
		double radius = grabit_ui_radius(GUI_R_BTN);

		if (selected)
			cairo_set_source_rgba(cr, 0.20, 0.45, 0.85, hovered ? 0.95 : 0.8);
		else if (hovered)
			cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
		else
			cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
		grabit_cairo_rect_r(cr, c->r.x, c->r.y, c->r.w, c->r.h, radius);
		cairo_fill(cr);

		if (hovered) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.75);
			cairo_set_line_width(cr, 1.4);
			grabit_cairo_rect_r_inset(cr, c->r.x, c->r.y, c->r.w, c->r.h, radius,
									  0.7);
			cairo_stroke(cr);
		}

		double cy = c->r.y + c->r.h / 2.0;
		double icon_cx = c->r.x + MENU_CHIP_PAD + MENU_ICON / 2.0;
		glyph_draw(cr, c, icon_cx, cy, MENU_ICON / 2.0 - 1.0, selected ? 1.0 : 0.9);

		const char *label = c->opt ? "Annotate" : menu_action_name((enum menu_action)c->id);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
							   selected ? CAIRO_FONT_WEIGHT_BOLD
										: CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, MENU_LABEL_SIZE);
		cairo_set_source_rgba(cr, 1, 1, 1, selected ? 1.0 : 0.85);
		cairo_move_to(cr, c->r.x + MENU_CHIP_PAD + MENU_ICON + MENU_ICON_GAP,
					  cy + 4.0);
		cairo_show_text(cr, label);
	}

	cairo_restore(cr);
}
