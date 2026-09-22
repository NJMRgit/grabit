---
title: "grabit design documents"
section: root
doc_type: index
id: "grabit/index"
description: >
  Entry point for the grabit design documents: one per flow, each declaring
  the sources it governs.
status: current
updated: "2026-09-22"
tags: [index, spec, grabit]
---

# grabit design documents

The behavioural specification for grabit. Each document declares the files it
governs in its `source_files` frontmatter, so a change to a governed file must
update its document (the `~/Projects/dev-standard` rule, enforced by
`scripts/dev/spec-gate`).

- [capture-flow.md](capture-flow.md) — backend selection, region selection, the
  action bar and what an action does.
- [recording.md](recording.md) — the recording pipeline and its controls, and
  pinned captures.
- [services.md](services.md) — clipboard, notifications, sound, OCR, config and
  the plugin system.
- [testing.md](testing.md) — what `make test` covers and what needs a session.
