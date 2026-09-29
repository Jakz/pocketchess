# ADR 0010 — Engine lineup on the device: Stockfish 19 main, Stockfish 11 for Eco

- Status: Accepted (2026-09-29). Completes ADR 0004 (checkpoint) and the D8 benchmark decision.

## Decision

- **Stockfish 19** (net beside the binary, ADR 0009) is the engine for every role in the
  Performance and Balanced profiles.
- **Stockfish 11** is bundled as the light engine: the Eco profile uses it for play, hints and
  validation (`lightEngineForPlay`, lowest `energy_class`); analysis keeps Stockfish 19.
- **Stockfish 17.1** was evaluated and is not bundled (pin and descriptor removed; see below).
- `projects/engines.txt`: `stockfish`, `stockfish11`. Bundle ~101 MB, inside the 150 MB budget.

## Measurements (PB634K3, firmware 6.10.2544, Balanced profile, 1 thread, Hash 16)

Device benchmark (`logs/engine-bench.yaml`, 6 positions per row), two runs agree:

| Engine | readyok | RSS after ready / peak | knps | Depth: 0.5 s play / 1.5 s analysis |
| --- | --- | --- | --- | --- |
| Stockfish 19 | 2.3–2.7 s | 156 / 221 MiB | 17–30 | ~10.7 / ~12.3 |
| Stockfish 17.1 | 2.6 s | 172 / 267 MiB | 12–21 | ~9.5 / ~12.0 |
| Stockfish 11 | 0.7 s | 37 / 37 MiB | 156–219 | ~13.3 / ~17.2 |

- ADR 0004 checkpoint: Stockfish 19 passes (readyok < 3 s, peak RSS < 250 MB), only with the net
  beside the binary (embedded: +95 MB).
- CPU-seconds per move equal the movetime for every engine: with time budgets no engine saves
  energy per move. Stockfish 19's start costs ~2.7 CPU-s (loading the net).
- Strength at equal device time, measured on the desktop with the device's node rates (1 s per
  move: Stockfish 19 at 20k nodes vs Stockfish 11 at 200k nodes, 16 openings × both colours,
  adjudicated by the engines' scores): **Stockfish 19 +25 =7 −0** (~+360 Elo, 32 games).

## Why

- NNUE is ~10× slower per node than the classical evaluation on the 32-bit Cortex-A7 (2× on x64),
  for 17.1 as much as for 19, so the threat features are not the cause. Even so, Stockfish 19 at a
  tenth of the nodes is far stronger, so it is the right engine whenever it is loaded.
- Stockfish 11 saves 6× RAM and ~2 s of start-up (and the ~2.7 CPU-s net load). That matters in
  the Eco profile, which quits the engine after every job, and on a reader with ~470 MiB free
  while the app runs. At the beginner/casual levels strength is irrelevant; at club/strong the
  UCI_Elo limit caps both engines.
- Stockfish 17.1 is slower than 19 on the device and needs more RAM: no reason to keep it.

## Consequences

- Descriptors' `resources:` now carry the device numbers (shown in Settings from M3).
- The Eco profile's node limits (50k–100k) suit Stockfish 11 (~0.25–0.5 s on the device); if a
  user forces Stockfish 19 in Eco, the same limits take several seconds (they stay caps).
- `StockfishNnue.cmake` stays shared, so another NNUE pin can be benchmarked again by adding
  `engines/<id>/` + a descriptor.
- Open: battery % over 30 minutes Eco vs Balanced (M2 checklist) to confirm the Eco choice.
