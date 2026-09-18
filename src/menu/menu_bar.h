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

#define MENU_BAR_H 56
#define MENU_BAR_PAD 8
#define MENU_CHIP_PAD 10
#define MENU_ICON 18
#define MENU_ICON_GAP 7
#define MENU_GAP 6
#define MENU_DOCK_GAP 10
#define MENU_EDGE_GAP 8
#define MENU_LABEL_SIZE 12.5

struct menu_chip {
	struct rect r;
	int id;
	bool opt;
};

struct menu_bar {
	enum menu_action act;
	bool annotate;

	const struct grabit_output *go;
	int32_t w;
	int32_t h;
	struct rect bar;
	bool placed;

	struct menu_chip chips[MENU_CHIP_COUNT];
	int n;
	int hover;
};

bool menu_action_uses_edit(enum menu_action a);
const char *menu_action_name(enum menu_action a);

/*
 * The selector draws the bar inside its own overlay (so the bar never lands in
 * the capture and keeps working while the region is being picked). The app
 * layer sets the current bar before it starts a capture and reads the result
 * back afterwards.
 */
void menu_bar_set_current(struct menu_bar *mb);
struct menu_bar *menu_bar_current(void);

void menu_bar_init(struct menu_bar *mb, struct config *cfg);
void menu_bar_place(struct menu_bar *mb, const struct grabit_output *go);

/* hooks used by the region selector */
void menu_bar_render(cairo_t *cr, struct menu_bar *mb, const struct grabit_output *go,
					 double scale);
bool menu_bar_hover(struct menu_bar *mb, int32_t x, int32_t y);
bool menu_bar_press(struct menu_bar *mb, int32_t x, int32_t y);

#endif
