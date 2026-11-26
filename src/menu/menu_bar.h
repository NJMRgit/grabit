// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_MENU_BAR_H
#define GRABIT_MENU_BAR_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo/cairo.h>

#include "region/region.h"

struct config;
struct grabit_output;
struct grabit_wl_state;

enum menu_action {
	MA_COPY = 0,
	MA_SAVE,
	MA_OCR,
	MA_TRANSLATE,
	MA_UPLOAD,
	MA_PIN,
	MA_RECORD,
	MA_ACTION_COUNT,
};

#define MENU_CHIP_COUNT (MA_ACTION_COUNT + 1)
#define MENU_OP_ANNOTATE MENU_CHIP_COUNT - 1

#define MENU_MONITOR_MAX 16

#define MENU_BAR_H 56
#define MENU_BAR_PAD 8
#define MENU_CHIP_PAD 10
#define MENU_ICON 18
#define MENU_ICON_GAP 7
#define MENU_CHECKBOX 14
#define MENU_GAP 6
#define MENU_EDGE_GAP 8
/* the tab hanging off the bottom edge, sized from the bar it opens */
#define MENU_HANDLE_H 12
/* how long the bar takes to grow out of its tab, and the tab to rise up from
   the bottom edge, in milliseconds */
#define MENU_ANIM_MS 150
#define MENU_RISE_MS 240
#define MENU_HANDLE_MIN_W 72
#define MENU_HANDLE_MAX_W 240
#define MENU_LABEL_SIZE 12.5

struct menu_chip {
	struct rect r;
	int id;
	bool opt;
};

/* A full-screen button on the left of the bar. The target is copied when the
   bar is placed, so it stays valid after the bar's wayland state is gone. */
struct menu_monitor {
	struct rect chip;
	struct rect target;
	char label[32];
};

/* what a click on the bar asked the selector to do */
enum menu_press {
	MENU_PRESS_NONE = 0,
	MENU_PRESS_HANDLED,	   /* open/close, hover bookkeeping, annotate toggle */
	MENU_PRESS_ACTION,	   /* an action chip was chosen: confirm the selection */
	MENU_PRESS_FULLSCREEN, /* mb->pending_fs holds the monitor to capture */
	MENU_PRESS_CONFIRM,	   /* the confirm checkbox was toggled */
};

struct menu_bar {
	enum menu_action act;
	bool annotate;
	bool confirm; /* adjust the region before capturing it */
	struct config *cfg;

	int32_t w;
	int32_t h;
	struct rect bar;
	struct rect handle;
	struct rect go_rect;
	char go_name[64];
	bool open;	 /* the target state: false folds the bar back into its tab */
	double k;	 /* 0 folded, 1 open: what the open/close animation draws */
	double rise; /* 0 below the bottom edge, 1 in place: the tab's own rise */
	double k_from;
	double rise_from;
	int64_t k_start_ns;
	int64_t rise_start_ns;
	bool started;
	bool placed;

	struct menu_chip chips[MENU_CHIP_COUNT];
	int n;
	int hover;
	int32_t chip_pad;
	bool monitor_labels;

	struct menu_monitor monitors[MENU_MONITOR_MAX];
	int n_monitors;
	int hover_monitor;
	struct rect confirm_chip;
	bool hover_confirm;
	struct rect pending_fs;
};

bool menu_action_uses_edit(enum menu_action a);
const char *menu_action_name(enum menu_action a);

/*
 * The selector draws the bar inside its own overlay, so it never lands in the
 * capture and keeps working while the region is being picked. The app layer
 * sets the current bar before it starts a capture and reads the result back
 * afterwards.
 */
void menu_bar_set_current(struct menu_bar *mb);
struct menu_bar *menu_bar_current(void);

void menu_bar_init(struct menu_bar *mb, struct config *cfg);
void menu_bar_place(struct menu_bar *mb, const struct grabit_output *go,
					struct grabit_wl_state *wls);
void menu_bar_set_open(struct menu_bar *mb, bool open);
bool menu_bar_tick(struct menu_bar *mb);
/* every pixel the bar and its tab can touch: the redraw damage of a frame */
bool menu_bar_damage_rect(const struct menu_bar *mb, struct rect *out);

/* hooks used by the region selector, which draws the bar in its own overlay */
void menu_bar_render(cairo_t *cr, struct menu_bar *mb, const struct grabit_output *go,
					 double scale);
bool menu_bar_hover(struct menu_bar *mb, int32_t x, int32_t y);
enum menu_press menu_bar_press(struct menu_bar *mb, int32_t x, int32_t y);

#endif
