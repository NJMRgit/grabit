// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_MENU_H
#define GRABIT_MENU_H

struct config;
struct args;

/*
 * `grabit --menu`: freeze the screen, show the action bar inside the region
 * selector, and run the picked action on the region the user selects.
 */
int gapp_run_menu(struct config *cfg, const struct args *a);

#endif
