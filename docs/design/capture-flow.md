---
title: "grabit — the capture flow"
section: Backend
doc_type: spec
id: "grabit/capture-flow"
description: >
  How grabit picks a capture backend, how a region is selected, what the menu
  action bar does and what each action produces.
status: current
updated: "2026-11-25"
source_files:
  - src/capture
  - src/region
  - src/menu
  - src/clipboard
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

Confirm mode is on by default. Releasing a region drag leaves the selection
adjustable with eight resize handles instead of capturing straight away; the
user confirms with `Enter`, `Ctrl+C`, a double-click inside the box, or by
clicking an action chip. The same selection rule applies to a region picked for
`--record`, so a recording also waits for confirmation and can be resized first.

Two controls frame the dock: a full-screen button for every monitor on the very
left, and a **Confirm** checkbox on the very right. A monitor button captures
that whole output with the currently selected action and closes the dock. The
checkbox toggles the confirmation behaviour for the live session and saves the
value back to `region.confirm`, so a later plain region capture or `--record`
uses the same choice.

`src/region/` owns the drag geometry, handles and the freeze; `capture/freeze.*`
and `capture/edit_file.*` hold the frozen frame the region is cut from;
`src/menu/` owns the dock, its monitor buttons and the confirm toggle.

Screenshots leave the mouse pointer out by default (`capture.cursor` is off);
`--cursor` or `capture.cursor=true` includes it again. Recording keeps its own
`recording.cursor` setting, which stays on by default, so a screen recording
still shows the pointer even when screenshots do not.

The overlay is composited, not recomposited: `region/render_buffer.c` scales the
frozen frame once (and once more into a dimmed copy), and a frame paints and
damages only the rect it recorded, so the action bar's fold-out costs the bar's
pixels instead of the whole screen. The surfaces render at the output's real
pixel ratio — `wp_fractional_scale_v1` and a `wp_viewport` destination, so a
1.5-scaled panel gets a 1.5x buffer rather than a 2x one the compositor scales
back down; compositors without those protocols keep the integer-scale path.

## Actions

`app/dispatch.c` decides which action a key or the menu picked and
`app/actions.c` runs it; `app/source.c` resolves what is being captured (a
monitor, an output, a region, a window).

- **copy** — to the clipboard, preferring the compositor's own image copy path
  (`capture/ext_image_copy.c`) and falling back to the session's
  (`capture/ext_session.c`). The image is offered as its real MIME type together
  with KDE's `x-kde-force-image-copy` marker, so Klipper records an explicit
  image copy even when its optional image history is off.
- **save** — PNG by default, JPEG where asked (`capture/jpeg.c`).
- **pin** — see [recording.md](recording.md).
- **OCR / translate / upload** — see [services.md](services.md).
- **record** — see [recording.md](recording.md).

A capture that could not be taken must report the backend that failed and why;
a silent empty image is not an acceptable outcome.
