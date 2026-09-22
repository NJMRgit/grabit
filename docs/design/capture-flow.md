---
title: "grabit — the capture flow"
section: Backend
doc_type: spec
id: "grabit/capture-flow"
description: >
  How grabit picks a capture backend, how a region is selected, what the menu
  action bar does and what each action produces.
status: current
updated: "2026-09-22"
source_files:
  - src/capture
  - src/region
  - src/app/actions.c
  - src/app/dispatch.c
  - src/args.c
related:
  - docs/design/recording.md
  - docs/design/services.md
tags: [spec, grabit, capture]
---

# The capture flow

## Backend selection

`capture.c` holds the capability ladder and reports why a rung is missing:

1. **wlr** — `wlr-screencopy` is advertised by the compositor (hyprland, sway,
   niri, river). The default on wlroots.
2. **kwin** — `org.kde.KWin.ScreenShot2` is on the bus (KDE Plasma). A rotated
   output is warned about rather than silently captured wrong.
3. **screencast** — the recording path only: kwin's screencast protocol or
   mutter's screencast D-Bus API, both through PipeWire and **without a portal
   dialog**. GNOME is recording-only.

X11 is not supported: there is no backend for it, and the ladder must fail
loudly rather than fall back to something that captures the wrong surface.

## Region selection

`-M` / `--menu` is the on-screen flow: the screen **freezes**, the user drags
the region, and an action bar appears with copy, save, OCR, translate, upload,
pin and record. The bar does not stand in the way afterwards — it folds into a
**grab tab on the bottom edge** until the pointer approaches it.

`src/region/` owns the drag geometry and the freeze; `capture/freeze.*` and
`capture/edit_file.*` hold the frozen frame the region is cut from.

## Actions

`app/dispatch.c` decides which action a key or the menu picked and
`app/actions.c` runs it; `app/source.c` resolves what is being captured (a
monitor, an output, a region, a window).

- **copy** — to the clipboard, preferring the compositor's own image copy path
  (`capture/ext_image_copy.c`) and falling back to the session's
  (`capture/ext_session.c`).
- **save** — PNG by default, JPEG where asked (`capture/jpeg.c`).
- **pin** — see [recording.md](recording.md).
- **OCR / translate / upload** — see [services.md](services.md).
- **record** — see [recording.md](recording.md).

A capture that could not be taken must report the backend that failed and why;
a silent empty image is not an acceptable outcome.
