---
title: "grabit — tests"
section: Validation
doc_type: spec
id: "grabit/testing"
description: >
  What `make test` checks, and which parts of grabit need a real session.
status: current
updated: "2026-09-22"
related:
  - docs/design/capture-flow.md
tags: [spec, grabit, tests]
---

# Tests

```
make test      # check_headers --check src  +  check-docs
```

`check_headers` enforces the C hygiene the project relies on (include guards,
prototypes, the header/source pairing) and `check-docs` validates the generated
documentation against the built binary — so an option or plugin command that
loses its documentation fails the build.

Not covered, and not coverable in-process: anything that needs a compositor,
a GPU, PipeWire, a clipboard owner, an OCR engine or a real capture backend.
Those are verified by running grabit in a session (all four supported
compositors are the compatibility contract); a test double for the compositor
would only pin the double.

`scripts/dev/spec-gate` runs `make test` and the spec-coherence checks together
and is the gate.
