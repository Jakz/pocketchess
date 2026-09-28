# AGENTS.md — conventions for humans and coding agents

PocketChess is an offline chess study app for the PocketBook Verse Pro Color (PB634K3, firmware
6.10), developed **in parallel** as a Windows desktop build (MSVC + SDL3 e-ink simulator) and a
PocketBook build (CMake + Ninja + Zig). Read `docs/ai/README.md` first, then the current milestone
in `docs/ai/04-milestones.md`, then `docs/project-status.md` (the resume point).

## Parity rules (D1, build doc §6)

1. Features live in `src/core`, `src/ui` or `src/app`. A file in `src/platform/<backend>` may only
   translate events, present pixels, spawn processes, or report power/lifecycle state.
2. Every new `.cpp` goes into **both** its `.vcxproj` (`projects/msvc/`) **and**
   `projects/pocketbook/sources.cmake`. `scripts/check-sources.ps1` runs before every MSVC build
   and fails on drift.
3. A feature is done only when both builds compile, the tests pass (`PocketChessTests` on Windows,
   `pocketchess-selftest.app` on the reader) and the milestone's device checklist has run.
4. `scripts/verify-all.ps1` is green before closing a milestone.

## Layering

```
platform/*  ->  app  ->  ui  ->  core  ->  (libs)
```

- `src/core`, `src/ui`, `src/app` never include `<SDL3/...>`, `<inkview.h>`, `<windows.h>`,
  `<unistd.h>`, `<sys/...>` or other OS headers (checked by `check-sources.ps1`).
- `core` knows nothing about pixels or input; `ui` knows nothing about SDL/InkView; `app` wires
  controllers to views and owns the screen stack.
- One UI thread. Background threads (engine readers) only `Host::post()` back to it.
- Views never present: they `invalidate(rect, hint)`; the `Application` redraws and presents once
  per event, and the `RefreshPolicy` picks Fast / Partial / Full.

## Naming and style

- Files `CamelCase.cpp/.h`, directories lowercase, namespaces `pchess::<dir>` (`pchess::chess`,
  `pchess::ui`, `pchess::app`, `pchess::platform::sdl` ...). C++20. 4-space indent, 120 columns
  (`.clang-format`).
- Every source file starts with `// SPDX-License-Identifier: GPL-3.0-or-later` (scripts: `#`).
- `int` for squares and indices; `uint64_t` for bitboards (never `long`). Code must be correct with
  **unsigned `char`** (ARM) — cast through `unsigned char` before arithmetic on bytes.
- No FMA-sensitive math in drawing: builds use `-ffp-contract=off` / `/fp:precise` so the canvas is
  bit-identical on x64 and ARM (golden checksums).

## Errors

- Recoverable failures return `pchess::Result<T>` / `pchess::Error{code, context}`; no exceptions
  across module boundaries. Screens show a dialog with a remedy; the app never exits on content
  errors (design notes §32).
- Log with `util::Log` (`logs/pocketchess.log`); every job/measurement worth tuning is logged.

## Data

- **Never overwrite `user/`** (progress, settings, session). Deploy and staging scripts exclude it.
- Content packs, engines, piece sets and themes are swappable units with manifests (D8, D9).
- Builds never download anything. Only `scripts/fetch-engines.ps1` (and later `fetch-lichess.ps1`)
  touch the network.

## Tests

- Unit tests use the in-repo framework in `tests/support/Test.h` (doctest-compatible subset).
  Add tests next to the code they cover (`tests/core`, `tests/ui`, `tests/app`).
- Golden images: `PocketChessTests --update-golden` rewrites `tests/golden/golden.txt` and the PNGs;
  review the PNGs before committing. The device self-test compares the same hashes.
- Deterministic: time through `util::Clock`, randomness through `util::Random`, files only in temp dirs.

## Documentation duty

- Update `docs/project-status.md` ("Working now / Not working yet / Resume point / Verified on
  hardware") at the end of every work session.
- Record decisions as ADRs in `docs/adr/NNNN-title.md`; reference plan IDs (D*, P*, R*).
- Device behaviour (refresh modes, key codes, timings) goes into the "Verified on hardware" table
  with the date and firmware.
