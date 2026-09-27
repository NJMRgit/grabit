---
title: "grabit — recording and pinned captures"
section: Backend
doc_type: spec
id: "grabit/recording"
description: >
  The recording pipeline, its control bar, and how a pinned capture behaves.
status: current
updated: "2026-09-27"
source_files:
  - src/record
  - src/pin
  - src/app/source.c
related:
  - docs/design/capture-flow.md
tags: [spec, grabit, recording, pin]
---

# Recording and pinned captures

## The pipeline

`record/loop.c` drives the capture-to-encode loop, `record/ffmpeg.c` owns the
encoder process (its arguments and its exit status), and `record/compose.c`
composes the frames. A recording ends when the user stops it from the controls
or when the source disappears; the file is finalized rather than left partial.

## The controls

The controls (`record/controls*.c`) stay out of the way but reachable in every
case, and always **start as a small handle that rises out of the top edge of the
region and grows into the bar on hover**, folded back into the handle when the
pointer leaves. The recording border and the control bar are not drawn onto
full-screen layer surfaces, because a compositor (kwin) blurs the whole screen
when they are.

## Pinned captures

A pinned capture (`src/pin/`) is an interactive on-screen copy of an image, not
a static overlay: hovering it shows a close button, and dragging its body moves
it without entering a grab mode. A pin must not take pointer focus away from
what is under it until the pointer is actually over the pin.
