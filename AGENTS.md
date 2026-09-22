# AGENTS.md — guide for agents working on grabit

## Tests and spec coherence (dev-standard)

This project follows `~/Projects/dev-standard`: **tests are part of the change**
and **the spec and the code move together**.

- `make test` (header hygiene + docs check) must pass. `make all` must build.
- Run the gate before reporting work as done:

  ```sh
  scripts/dev/spec-gate          # test command + (once docs exist) coherence
  ```

  Exit 1 means blocked. When a change genuinely is not a behaviour change, put a
  `Spec-Override: <reason>` trailer on the commit.
- The project is at **L1** (tests). It gains L2/L3 once design documents with
  `source_files` frontmatter exist under `docs/`; `~/Projects/dev-standard/README.md`
  has the contract and the adoption checklist.
