# Stockfish 17.1 plug-in (candidate) — build notes

Status: sources and both nets fetched and pinned (sf_17.1, 2026-09-29). Built with the shared
`../StockfishNnue.cmake` (same flags as Stockfish 19: `armv7-neon` / `x86-64-avx2`, re-checked
against the 17.1 Makefile). Windows: `bench 16 1 8` → 103119 nodes, ~1.59 Mnps (Stockfish 19:
~1.73 Mnps). The arm build links; the device benchmark is pending.

Why it is here: on the reader Stockfish 19 runs ~20 knps against ~200 knps for Stockfish 11
(2× on x64). 17.1 is the last release before the threat input features, with a big net
(`nn-1c0000000000.nnue`, 74.9 MB) and a small one (`nn-37f18f62d772.nnue`, 3.5 MB) that it uses
for clearly unbalanced positions. Both ship beside the binary and are listed in `files:`.

Bundle budget (architecture doc §13, 150 MB): Stockfish 19 (99.5 MB) + 17.1 (79.3 MB) together
exceed it; after the benchmark only one NNUE Stockfish stays in `projects/engines.txt`.
