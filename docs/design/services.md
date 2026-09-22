---
title: "grabit — clipboard, notifications, sound, OCR and plugins"
section: Backend
doc_type: spec
id: "grabit/services"
description: >
  The side services a capture can reach: clipboard, desktop notifications,
  sound, OCR/translate/upload, configuration and the plugin system.
status: current
updated: "2026-09-22"
source_files:
  - src/clipboard
  - src/notify
  - src/sound
  - src/ocr
  - src/app/ocr.c
  - src/plugin
  - src/config
related:
  - docs/design/capture-flow.md
tags: [spec, grabit, services, plugins]
---

# Services

## Clipboard

`src/clipboard/` owns the image/text copy. The compositor's own image copy is
preferred where it exists; the session's clipboard is the fallback. A copy that
cannot be delivered must fail visibly — a screenshot the user believes is on
the clipboard but is not is the worst outcome of this flow.

## Notifications

`src/notify/` sends the desktop notification for a finished action (saved,
copied, uploaded, OCR'd). Notifications are informational: an action that works
does not depend on them, and a missing notification daemon must not fail the
action.

## Sound

`src/sound/` plays the shutter/feedback sound. It is best-effort and never
gates an action.

## OCR, translate and upload

`src/ocr/` (with `app/ocr.c` for the dispatch) runs the OCR of the selected
region; translate and upload are the follow-on actions of the same menu. Each
reports its result — the recognized text to the clipboard or the upload URL to
the user — and reports failure in place instead of dropping it.

## Configuration

`src/config/` reads the configuration (paths honouring `$XDG_CONFIG_HOME` and
`$XDG_CACHE_HOME`). Missing keys take documented defaults; the file is never
rewritten behind the user's back.

## Plugins

`src/plugin/` dispatches `grabit <name> [args]` to a standalone plugin binary.
A plugin ships as a git repo with a `manifest.toml`, is built in-tree or fetched
as a prebuilt release, and lives under `~/.config/grabit/plugins/<name>/` with
its binary symlinked into `.bin/grabit-<name>`. The install/remove/update lock
is `~/.config/grabit/plugins/.lock`, the state is `.source` (kind/url/sha256)
and `.last_check` drives auto-update scheduling. Plugin names must match
`[a-z0-9_-]+`. `PLUGINS.md` at the repo root is the user-facing reference for
the same contract.
