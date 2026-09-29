# ADR 0004 — Engine choice and the M0.6 checkpoint

- Status: Accepted (2026-09-29): the checkpoint passed on the device; the lineup is in ADR 0010. Implements D4, D8.

## Decision (so far)

- Stockfish 19 is the default engine, one plug-in among others (`projects/engines.txt`,
  `data/engines/stockfish/engine.yaml`, `projects/pocketbook/engines/stockfish/`).
- Sources and networks are local only (`scripts/fetch-engines.ps1`, pin in `engine.lock`; net
  files are verified by the SHA-256 prefix in their names).
- The engine is reached only through `engine::Process` (core) and the UCI parser; the M0.6 probe
  (`engine::UciProbe`, Diagnostics → Engine) measures time to uciok/readyok, RSS after load,
  depth/nodes/nps for `go movetime 1000`, CPU-seconds and peak RSS.

## Checkpoint

Stockfish stays the default for every role if readyok < 3 s and RSS < 250 MB on the PB634K3.
Otherwise it is kept for explicit Deep analysis only and the Eco engine work (Stockfish 11) moves
up from M2. Record the numbers here and in `docs/project-status.md`.
