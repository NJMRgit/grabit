// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "menu/menu.h"
#include "menu/menu_bar.h"

#include "app/app.h"
#include "args.h"
#include "config/config.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "record/record.h"
#include "sound/sound.h"
#include "util/util.h"
#include "wl/wl.h"
#include "wm/wm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int copy_file(const char *src, const char *dst) {
	FILE *in = fopen(src, "rb");
	if (!in) return -1;
	FILE *out = fopen(dst, "wb");
	if (!out) {
		fclose(in);
		return -1;
	}
	char buf[65536];
	int rc = 0;
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			rc = -1;
			break;
		}
	}
	if (ferror(in)) rc = -1;
	if (fclose(out) != 0) rc = -1;
	fclose(in);
	if (rc != 0) unlink(dst);
	return rc;
}

static int menu_save(struct config *cfg, const struct args *a, const char *path) {
	if (a->edit) {
		/* the editor writes into the save directory itself */
		bool tmp = false;
		char *out = gapp_acquire_source(a, cfg, ACTION_OUTPUT, &tmp, NULL);
		if (!out) return 1;
		puts(out);
		notify_send(&(struct notify_opts){
			.summary = "Saved",
			.body = grabit_basename(out),
			.icon_path = out,
		});
		grabit_sound_play(cfg);
		gapp_maybe_show_preview(cfg, out, grabit_basename(out), NULL);
		fflush(stdout);
		gapp_release_source(out, tmp);
		return 0;
	}

	char *dst = paths_build_output(cfg, a->filename_tpl, ".png", PATHS_DEST_PICTURES);
	if (!dst) {
		log_error("menu: could not build a save path");
		return 1;
	}
	if (copy_file(path, dst) != 0) {
		log_error("menu: could not save %s to %s: %s", path, dst, strerror(errno));
		notify_send(&(struct notify_opts){
			.summary = "grabit: save failed",
			.body = "could not write the file",
			.force = true,
		});
		free(dst);
		return 1;
	}
	puts(dst);
	fflush(stdout);
	notify_send(&(struct notify_opts){
		.summary = "Saved",
		.body = grabit_basename(dst),
		.icon_path = dst,
	});
	grabit_sound_play(cfg);
	gapp_maybe_show_preview(cfg, dst, grabit_basename(dst), NULL);
	free(dst);
	return 0;
}

static const struct grabit_output *pick_output(struct grabit_wl_state *s) {
	struct grabit_output *o = grabit_wm_active_output(s);
	if (!o) o = grabit_wl_primary_output(s);
	if (!o && s->n_outputs > 0) o = s->outputs[0];
	return o;
}

int gapp_run_menu(struct config *cfg, const struct args *a) {
	struct grabit_wl_state s;
	struct menu_bar mb;
	menu_bar_init(&mb, cfg);

	if (grabit_wl_init(&s) != 0) {
		log_error("menu: could not connect to the wayland compositor");
		notify_send(&(struct notify_opts){
			.summary = "grabit: menu unavailable",
			.body = "could not connect to the wayland compositor",
			.force = true,
		});
		return 1;
	}
	if (!s.layer_shell || !s.compositor || s.n_outputs == 0) {
		log_error("menu: the compositor has no wlr-layer-shell; use the flags instead "
				  "(grabit -c, -u, -o, --tesseract, --record)");
		notify_send(&(struct notify_opts){
			.summary = "grabit: menu unavailable",
			.body = "this compositor has no layer-shell support",
			.force = true,
		});
		grabit_wl_finish(&s);
		return 1;
	}
	const struct grabit_output *go = pick_output(&s);
	menu_bar_place(&mb, go);
	log_debug("menu: bar %dx%d at %d,%d on %s, action=%d", mb.w, mb.h, mb.bar.x,
			  mb.bar.y, go && go->name ? go->name : "?", (int)mb.act);
	grabit_wl_finish(&s);

	struct args cap = *a;
	cap.action = ACTION_COPY;
	cap.file = NULL;
	cap.fullscreen = false;
	cap.fullscreen_target = NULL;
	cap.window = false;
	cap.last_region = false;
	cap.no_last = false;
	cap.edit = false;
	cap.translate = false;
	cap.show = false;
	cap.format = "png";

	menu_bar_set_current(&mb);
	bool is_temp = false;
	struct rect rect = {0};
	char *path = gapp_capture_to_file(&cap, cfg, ACTION_COPY, &is_temp, &rect);
	menu_bar_set_current(NULL);

	enum menu_action act = mb.act;
	bool annotate = mb.annotate;
	if (!path) {
		log_info("menu: cancelled");
		return 1;
	}
	log_info("menu: action=%d annotate=%d rect=%d,%d %dx%d", (int)act, (int)annotate,
			 rect.x, rect.y, rect.w, rect.h);

	struct args act_args = *a;
	act_args.action = ACTION_COPY;
	act_args.file = path;
	act_args.edit = annotate && menu_action_uses_edit(act);
	act_args.translate = act == MA_TRANSLATE;
	act_args.fullscreen = false;
	act_args.fullscreen_target = NULL;
	act_args.window = false;
	act_args.no_last = false;
	act_args.last_region = true;

	int rc = 1;
	switch (act) {
	case MA_COPY:
		rc = gapp_run_copy(cfg, &act_args);
		break;
	case MA_SAVE:
		rc = menu_save(cfg, &act_args, path);
		break;
	case MA_OCR:
	case MA_TRANSLATE:
		rc = gapp_run_ocr(cfg, &act_args);
		break;
	case MA_UPLOAD:
		rc = gapp_run_upload(cfg, &act_args);
		break;
	case MA_PIN:
		rc = gapp_run_pin(cfg, &act_args);
		break;
	case MA_RECORD:
		/* the region just picked is stored as region.last; record it live */
		act_args.file = NULL;
		act_args.edit = false;
		unlink(path);
		gapp_clear_tmpfile();
		rc = record_toggle(cfg, &act_args);
		break;
	default:
		break;
	}

	if (act != MA_RECORD)
		gapp_release_source(path, is_temp);
	else
		free(path);
	return rc;
}
