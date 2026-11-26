// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
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
#include <time.h>

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
	mb->hover_monitor = -1;
	mb->confirm = true;
	mb->cfg = cfg;
	mb->open = false;
	mb->k = 0.0;
	mb->rise = 0.0;
	mb->started = false;
	if (cfg) {
		const char *confirm = config_get(cfg, "region.confirm");
		if (confirm) mb->confirm = strcmp(confirm, "true") == 0;
	}
	const char *name = cfg ? config_get(cfg, "default_action") : NULL;
	if (name) {
		if (strcmp(name, "upload") == 0)
			mb->act = MA_UPLOAD;
		else if (strcmp(name, "save") == 0)
			mb->act = MA_SAVE;
		else if (strcmp(name, "pin") == 0)
			mb->act = MA_PIN;
	}
}

static int32_t menu_chip_width(const char *label, int32_t pad, bool icon,
							   double size) {
	int32_t w = pad * 2 + text_width(label, size, false);
	if (icon) w += MENU_ICON + MENU_ICON_GAP;
	return w;
}

/* Lay the monitor chips on the left, the actions in the middle and the confirm
   checkbox on the right. Returns the width the bar needs. */
static int32_t menu_bar_layout(struct menu_bar *mb, int32_t pad, bool monitor_labels) {
	int32_t chip_h = MENU_BAR_H - MENU_BAR_PAD * 2;
	int32_t x = MENU_BAR_PAD;

	mb->chip_pad = pad;
	mb->monitor_labels = monitor_labels;
	for (int i = 0; i < mb->n_monitors; i++) {
		struct menu_monitor *m = &mb->monitors[i];
		int32_t cw = monitor_labels
						 ? menu_chip_width(m->label, pad, true, MENU_LABEL_SIZE)
						 : pad * 2 + MENU_ICON;
		m->chip = (struct rect){x, MENU_BAR_PAD, cw, chip_h};
		x += cw + MENU_GAP;
	}
	if (mb->n_monitors > 0) x += 4;

	mb->n = 0;
	for (int i = 0; i < MENU_CHIP_COUNT; i++) {
		const char *label = i == MENU_OP_ANNOTATE ? "Annotate"
												  : menu_action_name((enum menu_action)i);
		int32_t cw = menu_chip_width(label, pad, true, MENU_LABEL_SIZE);
		mb->chips[mb->n] = (struct menu_chip){
			.r = {x, MENU_BAR_PAD, cw, chip_h},
			.id = i,
			.opt = i == MENU_OP_ANNOTATE,
		};
		mb->n++;
		x += cw;
		if (i + 1 < MENU_CHIP_COUNT) x += MENU_GAP;
	}

	x += 10;
	int32_t cw = menu_chip_width("Confirm", pad, false, MENU_LABEL_SIZE) +
				 MENU_CHECKBOX + MENU_ICON_GAP;
	mb->confirm_chip = (struct rect){x, MENU_BAR_PAD, cw, chip_h};
	x += cw;
	return x + MENU_BAR_PAD;
}

void menu_bar_place(struct menu_bar *mb, const struct grabit_output *go,
					struct grabit_wl_state *wls) {
	mb->n_monitors = 0;
	if (wls) {
		for (size_t i = 0; i < wls->n_outputs && mb->n_monitors < MENU_MONITOR_MAX; i++) {
			const struct grabit_output *o = wls->outputs[i];
			if (o->dead) continue;
			struct menu_monitor *m = &mb->monitors[mb->n_monitors++];
			grabit_output_rect(o, &m->target);
			snprintf(m->label, sizeof m->label, "%s",
					 o->name && o->name[0] ? o->name : "Screen");
		}
	}
	if (mb->n_monitors == 1)
		snprintf(mb->monitors[0].label, sizeof mb->monitors[0].label, "Fullscreen");

	int32_t avail = go ? go->logical_width - 2 * MENU_EDGE_GAP : 0;
	int32_t w = 0;
	for (int pass = 0; pass < 3; pass++) {
		int32_t pad = pass == 0 ? MENU_CHIP_PAD : 6;
		bool labels = pass < 2;
		w = menu_bar_layout(mb, pad, labels);
		if (avail <= 0 || w <= avail) break;
	}
	mb->w = w;

	mb->h = MENU_BAR_H;
	int32_t hw = mb->w / 5;
	if (hw < MENU_HANDLE_MIN_W) hw = MENU_HANDLE_MIN_W;
	if (hw > MENU_HANDLE_MAX_W) hw = MENU_HANDLE_MAX_W;
	int32_t x = go->x + (go->logical_width - mb->w) / 2;
	int32_t y = go->y + go->logical_height - MENU_BAR_H;
	if (x < go->x + MENU_EDGE_GAP) x = go->x + MENU_EDGE_GAP;
	if (y < go->y + MENU_EDGE_GAP) y = go->y + MENU_EDGE_GAP;
	mb->go_rect = (struct rect){go->x, go->y, go->logical_width,
								go->logical_height};
	snprintf(mb->go_name, sizeof mb->go_name, "%s", go->name ? go->name : "");
	mb->bar = (struct rect){x, y, mb->w, MENU_BAR_H};
	/* the tab hangs off the bottom edge, centred under the bar: the bar opens
	   out of it the way the recording bar opens out of its handle */
	mb->handle = (struct rect){go->x + (go->logical_width - hw) / 2,
							   go->y + go->logical_height - MENU_HANDLE_H, hw,
							   MENU_HANDLE_H};
	mb->placed = true;
	for (int i = 0; i < mb->n_monitors; i++) {
		struct menu_monitor *m = &mb->monitors[i];
		log_debug("menu: monitor chip %s at %d,%d %dx%d", m->label,
				  mb->bar.x + m->chip.x, mb->bar.y + m->chip.y, m->chip.w,
				  m->chip.h);
	}
	for (int i = 0; i < mb->n; i++) {
		struct menu_chip *c = &mb->chips[i];
		log_debug("menu: chip %d (%s) at %d,%d %dx%d", i,
				  c->opt ? "Annotate" : menu_action_name((enum menu_action)c->id),
				  mb->bar.x + c->r.x, mb->bar.y + c->r.y, c->r.w, c->r.h);
	}
}

bool menu_bar_damage_rect(const struct menu_bar *mb, struct rect *out) {
	if (!mb || !mb->placed) return false;
	int32_t x0 = i32min(mb->bar.x, mb->handle.x) - 2;
	int32_t y0 = i32min(mb->bar.y, mb->handle.y) - 2;
	int32_t x1 = i32max(mb->bar.x + mb->bar.w, mb->handle.x + mb->handle.w) + 2;
	int32_t y1 = i32max(mb->bar.y + mb->bar.h, mb->handle.y + mb->handle.h) + 2;
	*out = rect_clamp_into((struct rect){x0, y0, x1 - x0, y1 - y0}, mb->go_rect);
	return out->w > 0 && out->h > 0;
}

static int64_t menu_now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static double menu_ease(double t) {
	if (t <= 0.0) return 0.0;
	if (t >= 1.0) return 1.0;
	return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t); /* ease out */
}

/* step one value towards its target; true while it still has frames to draw */
static bool menu_step(double *value, double target, double *from, int64_t *start_ns,
					  double duration_ms) {
	if (*value == target && *start_ns == 0) return false;
	double ms = (double)(menu_now_ns() - *start_ns) / 1e6;
	if (*start_ns == 0) {
		*from = *value;
		*start_ns = menu_now_ns();
		ms = 0.0;
	}
	if (ms >= duration_ms) {
		*value = target;
		*start_ns = 0;
		return true;
	}
	*value = *from + (target - *from) * menu_ease(ms / duration_ms);
	return true;
}

void menu_bar_set_open(struct menu_bar *mb, bool open) {
	if (!mb || mb->open == open) return;
	mb->open = open;
}

/* true while the bar or the tab still needs frames */
bool menu_bar_tick(struct menu_bar *mb) {
	if (!mb) return false;
	if (!mb->started) {
		/* the tab rises out of the bottom edge when the bar first appears */
		mb->started = true;
		mb->rise_from = mb->rise;
		mb->rise_start_ns = menu_now_ns();
	}
	bool busy = menu_step(&mb->k, mb->open ? 1.0 : 0.0, &mb->k_from, &mb->k_start_ns,
						  MENU_ANIM_MS);
	/* the tab only comes back once the bar has finished folding away */
	double rise_target = (mb->k <= 0.0 && !mb->open) ? 1.0 : 0.0;
	if (menu_step(&mb->rise, rise_target, &mb->rise_from, &mb->rise_start_ns,
				  MENU_RISE_MS))
		busy = true;
	return busy;
}

/* the tab keeps the bar open while the pointer crosses it */
static bool menu_open_area(const struct menu_bar *mb, int32_t x, int32_t y) {
	return rect_contains(mb->bar, x, y) || rect_contains(mb->handle, x, y);
}

static void menu_hover_clear(struct menu_bar *mb) {
	mb->hover = -1;
	mb->hover_monitor = -1;
	mb->hover_confirm = false;
}

static bool menu_chip_hit(const struct menu_bar *mb, struct rect local, int32_t x,
						  int32_t y) {
	local.x += mb->bar.x;
	local.y += mb->bar.y;
	return rect_contains(local, x, y);
}

bool menu_bar_hover(struct menu_bar *mb, int32_t x, int32_t y) {
	if (!mb->placed) return false;
	bool changed = false;

	if (!mb->open) {
		if (!rect_contains(mb->handle, x, y)) return false;
		menu_bar_set_open(mb, true);
		changed = true;
	} else if (!menu_open_area(mb, x, y)) {
		/* the pointer walked off the bar and its tab: fold it back to the tab */
		menu_bar_set_open(mb, false);
		menu_hover_clear(mb);
		return true;
	}

	int hover = -1;
	int hover_monitor = -1;
	bool hover_confirm = false;
	if (rect_contains(mb->bar, x, y)) {
		for (int i = 0; i < mb->n; i++) {
			if (menu_chip_hit(mb, mb->chips[i].r, x, y)) {
				hover = i;
				break;
			}
		}
		if (hover < 0) {
			for (int i = 0; i < mb->n_monitors; i++) {
				if (menu_chip_hit(mb, mb->monitors[i].chip, x, y)) {
					hover_monitor = i;
					break;
				}
			}
		}
		if (hover < 0 && hover_monitor < 0 &&
			menu_chip_hit(mb, mb->confirm_chip, x, y))
			hover_confirm = true;
	}
	if (hover == mb->hover && hover_monitor == mb->hover_monitor &&
		hover_confirm == mb->hover_confirm)
		return changed;
	mb->hover = hover;
	mb->hover_monitor = hover_monitor;
	mb->hover_confirm = hover_confirm;
	return true;
}

static void menu_confirm_store(struct menu_bar *mb) {
	if (!mb->cfg) return;
	const char *value = mb->confirm ? "true" : "false";
	if (config_set(mb->cfg, "region.confirm", value) != 0) {
		log_warn("menu: could not set region.confirm");
		return;
	}
	if (config_save(mb->cfg) != 0)
		log_warn("menu: could not save region.confirm");
}

enum menu_press menu_bar_press(struct menu_bar *mb, int32_t x, int32_t y) {
	if (!mb->placed) return MENU_PRESS_NONE;
	if (!mb->open) {
		/* touch has no hover: a tap on the tab opens the bar */
		if (!rect_contains(mb->handle, x, y)) return MENU_PRESS_NONE;
		log_debug("menu: tab click at %d,%d", x, y);
		menu_bar_set_open(mb, true);
		return MENU_PRESS_HANDLED;
	}
	if (!rect_contains(mb->bar, x, y)) {
		/* the tab is ours, the rest is not */
		return rect_contains(mb->handle, x, y) ? MENU_PRESS_HANDLED
											   : MENU_PRESS_NONE;
	}
	log_debug("menu: bar click at %d,%d", x, y);

	for (int i = 0; i < mb->n_monitors; i++) {
		if (!menu_chip_hit(mb, mb->monitors[i].chip, x, y)) continue;
		mb->pending_fs = mb->monitors[i].target;
		log_debug("menu: fullscreen chip %s at %d,%d %dx%d", mb->monitors[i].label,
				  mb->pending_fs.x, mb->pending_fs.y, mb->pending_fs.w,
				  mb->pending_fs.h);
		return MENU_PRESS_FULLSCREEN;
	}

	for (int i = 0; i < mb->n; i++) {
		if (!menu_chip_hit(mb, mb->chips[i].r, x, y)) continue;
		struct menu_chip *c = &mb->chips[i];
		log_debug("menu: bar chip %d (%s) %s", i,
				  c->opt ? "Annotate" : menu_action_name((enum menu_action)c->id),
				  c->opt ? "toggled" : "selected");
		if (c->opt) {
			mb->annotate = !mb->annotate;
			return MENU_PRESS_HANDLED;
		}
		mb->act = (enum menu_action)c->id;
		if (!menu_action_uses_edit(mb->act)) mb->annotate = false;
		return MENU_PRESS_ACTION;
	}

	if (menu_chip_hit(mb, mb->confirm_chip, x, y)) {
		mb->confirm = !mb->confirm;
		log_debug("menu: confirm checkbox %s", mb->confirm ? "on" : "off");
		menu_confirm_store(mb);
		return MENU_PRESS_CONFIRM;
	}
	return MENU_PRESS_HANDLED; /* click on the bar, but between chips: swallow it */
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

static void glyph_monitor(cairo_t *cr, double cx, double cy, double s) {
	cairo_set_line_width(cr, 1.6);
	grabit_cairo_rect_r(cr, cx - s, cy - s * 0.78, s * 2.0, s * 1.4, 1.8);
	cairo_stroke(cr);
	cairo_move_to(cr, cx, cy + s * 0.62);
	cairo_line_to(cr, cx, cy + s * 0.92);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s * 0.45, cy + s * 0.92);
	cairo_line_to(cr, cx + s * 0.45, cy + s * 0.92);
	cairo_stroke(cr);
}

static void glyph_checkbox(cairo_t *cr, double cx, double cy, double s, bool on) {
	double half = MENU_CHECKBOX / 2.0;
	if (on) {
		cairo_set_source_rgba(cr, 0.20, 0.45, 0.85, 1.0);
		grabit_cairo_rect_r(cr, cx - half, cy - half, half * 2, half * 2, 3.0);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		cairo_set_line_width(cr, 2.0);
		cairo_move_to(cr, cx - s * 0.5, cy);
		cairo_line_to(cr, cx - s * 0.1, cy + s * 0.4);
		cairo_line_to(cr, cx + s * 0.55, cy - s * 0.45);
		cairo_stroke(cr);
		return;
	}
	cairo_set_line_width(cr, 1.6);
	grabit_cairo_rect_r(cr, cx - half, cy - half, half * 2, half * 2, 3.0);
	cairo_stroke(cr);
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

/* flat against the bottom edge, rounded above: a tab hanging off the screen */
static void menu_handle_path(cairo_t *cr, double w, double h) {
	double r = h / 2.0;
	if (r > w / 2.0) r = w / 2.0;

	cairo_new_sub_path(cr);
	cairo_move_to(cr, 0, h);
	cairo_line_to(cr, 0, r);
	cairo_arc(cr, r, r, r, M_PI, 1.5 * M_PI);
	cairo_line_to(cr, w - r, 0);
	cairo_arc(cr, w - r, r, r, 1.5 * M_PI, 2.0 * M_PI);
	cairo_line_to(cr, w, h);
	cairo_close_path(cr);
}

/* the folded tab, drawn at the origin */
static void menu_tab_draw(cairo_t *cr, const struct menu_bar *mb) {
	double w = mb->handle.w;
	double h = mb->handle.h;
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.08, 0.08, 0.08, 0.94);
	menu_handle_path(cr, w, h);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	cairo_set_line_width(cr, 1.0);
	cairo_translate(cr, 0.5, 0.5);
	menu_handle_path(cr, w - 1.0, h - 1.0);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void menu_paint_chip(cairo_t *cr, struct rect r, bool active, bool hovered) {
	double radius = grabit_ui_radius(GUI_R_BTN);
	if (active)
		cairo_set_source_rgba(cr, 0.20, 0.45, 0.85, hovered ? 0.95 : 0.8);
	else if (hovered)
		cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	else
		cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	grabit_cairo_rect_r(cr, r.x, r.y, r.w, r.h, radius);
	cairo_fill(cr);

	if (!hovered) return;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.75);
	cairo_set_line_width(cr, 1.4);
	grabit_cairo_rect_r_inset(cr, r.x, r.y, r.w, r.h, radius, 0.7);
	cairo_stroke(cr);
}

static void menu_draw_label(cairo_t *cr, const char *label, double x, double cy,
							bool bold, double alpha) {
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
						   bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, MENU_LABEL_SIZE);
	cairo_set_source_rgba(cr, 1, 1, 1, alpha);
	cairo_move_to(cr, x, cy + 4.0);
	cairo_show_text(cr, label);
}

void menu_bar_render(cairo_t *cr, struct menu_bar *mb, const struct grabit_output *go,
					 double scale) {
	if (!mb->placed) return;
	if (mb->go_name[0]) {
		if (!go->name || strcmp(mb->go_name, go->name) != 0) return;
	} else if (go->x != mb->go_rect.x || go->y != mb->go_rect.y ||
			   go->logical_width != mb->go_rect.w ||
			   go->logical_height != mb->go_rect.h) {
		return;
	}

	/* the bar's rects are output-logical; the surface is buffer-scale pixels */
	cairo_save(cr);
	cairo_scale(cr, scale, scale);

	double k = mb->k;
	/* the tab rises up out of the bottom edge, and slips back through it while
	   the bar is on screen */
	if (mb->rise > 0.0) {
		cairo_save(cr);
		cairo_translate(cr, mb->handle.x - go->x,
						mb->handle.y - go->y + (1.0 - mb->rise) * mb->handle.h);
		menu_tab_draw(cr, mb);
		cairo_restore(cr);
	}
	if (k <= 0.0) {
		cairo_restore(cr);
		return;
	}

	/* the bar grows out of the tab: interpolate the two rects, both of which
	   sit on the bottom edge, and fade in on the way */
	double rx = mb->handle.x + (mb->bar.x - mb->handle.x) * k;
	double ry = mb->handle.y + (mb->bar.y - mb->handle.y) * k;
	double rw = mb->handle.w + (mb->bar.w - mb->handle.w) * k;
	double rh = mb->handle.h + (mb->bar.h - mb->handle.h) * k;
	cairo_save(cr);
	cairo_translate(cr, rx - go->x, ry - go->y);
	cairo_scale(cr, rw / mb->bar.w, rh / mb->bar.h);
	if (k < 1.0) cairo_push_group(cr);
	grabit_ui_panel(cr, 0, 0, mb->bar.w, mb->bar.h, 1.0);

	int32_t pad = mb->chip_pad;

	for (int i = 0; i < mb->n_monitors; i++) {
		const struct menu_monitor *m = &mb->monitors[i];
		bool hovered = mb->hover_monitor == i;
		menu_paint_chip(cr, m->chip, false, hovered);

		double cy = m->chip.y + m->chip.h / 2.0;
		double icon_cx = m->chip.x + pad + MENU_ICON / 2.0;
		cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
		glyph_monitor(cr, icon_cx, cy, MENU_ICON / 2.0 - 1.0);
		if (mb->monitor_labels)
			menu_draw_label(cr, m->label,
							m->chip.x + pad + MENU_ICON + MENU_ICON_GAP, cy, false,
							0.85);
	}

	for (int i = 0; i < mb->n; i++) {
		const struct menu_chip *c = &mb->chips[i];
		bool selected = c->opt ? mb->annotate : (int)mb->act == c->id;
		bool hovered = mb->hover == i;
		menu_paint_chip(cr, c->r, selected, hovered);

		double cy = c->r.y + c->r.h / 2.0;
		double icon_cx = c->r.x + pad + MENU_ICON / 2.0;
		glyph_draw(cr, c, icon_cx, cy, MENU_ICON / 2.0 - 1.0, selected ? 1.0 : 0.9);

		const char *label =
			c->opt ? "Annotate" : menu_action_name((enum menu_action)c->id);
		menu_draw_label(cr, label, c->r.x + pad + MENU_ICON + MENU_ICON_GAP, cy,
						selected, selected ? 1.0 : 0.85);
	}

	bool hovered = mb->hover_confirm;
	menu_paint_chip(cr, mb->confirm_chip, mb->confirm, hovered);
	double cy = mb->confirm_chip.y + mb->confirm_chip.h / 2.0;
	double box_cx = mb->confirm_chip.x + pad + MENU_CHECKBOX / 2.0;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	glyph_checkbox(cr, box_cx, cy, MENU_CHECKBOX / 2.0 - 1.0, mb->confirm);
	menu_draw_label(cr, "Confirm",
					mb->confirm_chip.x + pad + MENU_CHECKBOX + MENU_ICON_GAP, cy,
					mb->confirm, 0.9);

	if (k < 1.0) {
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, k);
	}
	cairo_restore(cr);

	cairo_restore(cr);
}
