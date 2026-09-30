# ADR 0013 — Import installs under user/

- Status: Accepted (2026-09-30). Implements design notes §34 for M6; plan IDs D8, D9.

## Context

Import (design notes §34) takes what the user copied into `imports/` over USB: PGN and FEN files,
game and puzzle packs, board themes, piece sets and engines. The bundled units live in `assets/`,
`engines/` and `content/`, which `deploy.ps1` mirrors with `robocopy /MIR`: anything the app
installed there would be deleted by the next deploy. `user/` is the one folder deploys and staging
never touch (AGENTS.md).

## Decision

- Import scans `imports/` only when the Import screen opens or on Rescan, never in the background,
  and classifies each entry: `.pgn`, `.fen`, `.db` (opened read-only: a game pack, else a puzzle
  pack), `.yaml` (a board theme), a folder with `pieceset.yaml` (all 12 SVGs must parse) or with
  `engine.yaml` (the binary must exist for this platform and its header must match the CPU:
  ELF/ARM on the reader, PE on Windows). Problems are shown per item with the reason (§32).
- Installing **moves** the entry to `user/<kind>/` (`util::Paths::installed`): `user/content/
  {games,puzzles}`, `user/themes`, `user/pieces`, `user/engines`. The app scans the bundled folder
  and then the user folder of each kind; an import never replaces or shadows a bundled or installed
  item with the same name or id.
- PGN and FEN files are opened in Analyze; a PGN can also become a game pack
  (`user/content/games/<stem>.db`) with the same `GameImporter` as the desktop `PgnImporter`,
  classified with the bundled `assets/openings` (Lichess ECO names, CC0, 388 KB).

## Consequences

- Deploys, updates and `-WithContent` never lose what the user imported; removing an import is a
  file operation in `user/` over USB.
- `EngineRegistry::scan` takes several folders; packs are looked up in both `content/<kind>` and
  `user/content/<kind>` (`Application::packFile`).
- Making a game pack runs on the UI thread (it is a one-off action; the time is logged). A progress
  view is left for later if large PGN files turn out to be common.
