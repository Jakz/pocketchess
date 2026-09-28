# PocketBook build

Cross-compiles `pocketchess.app` and `pocketchess-selftest.app` for the PocketBook Verse Pro Color
(PB634K3, firmware 6.10) with CMake + Ninja + the shared Zig toolchain (D3).

## Prerequisites

- Shared toolchain at `C:\Users\jack\Documents\Dev\libs\pocketbook` (Zig 0.16.0, InkView 6.10,
  `ZigToolchain.cmake`, `compat/`) — or pass `-ToolchainRoot` / set `POCKETBOOK_TOOLCHAIN_ROOT`.
- CMake + Ninja: Visual Studio's "C++ CMake tools for Windows" component, or both on `PATH`.
- Optional: `llvm-readelf` (VS "C++ Clang tools") for the NEEDED/GLIBC checks.

## Build

```powershell
projects\pocketbook\build-zig.ps1 -Configuration Release      # or build PocketBook in VS (Debug/Release)
```

Output: `out\pocketbook-zig\Release\` with the two `.app` files and `bundle\applications\`
(`pocketchess.app`, `pocketchess-selftest.app`, `pocketchess\` data tree). `check-elf.ps1` runs
automatically (ELF32, ARM, soft-float ABI).

## Deploy

```powershell
projects\pocketbook\deploy.ps1 -Configuration Release -Eject   # reader connected over USB
```

Copies to `\applications\` on the reader. `user\`, `logs\`, `imports\`, `exports\` on the device
are never touched; content packs only with `-WithContent`.

## Engines

`build-engine.ps1 -Target arm` (called by `build-zig.ps1` unless `-SkipEngines`) builds every id
in `projects\engines.txt` from `libs\<id>\`. Sources are local only: run
`scripts\fetch-engines.ps1` once. Missing sources only warn here, so a fresh clone still builds.

## Bundle layout

```text
applications/
├── pocketchess.app
├── pocketchess-selftest.app
└── pocketchess/
    ├── assets/fonts/        DejaVu Sans (+ Bold)
    ├── engines/<id>/        engine.yaml + binary (+ .nnue if not embedded)
    ├── selftest/golden.txt  golden canvas hashes for the self-test
    ├── content/             puzzle/game packs (M4/M5)
    ├── imports/ exports/    USB exchange folders
    ├── user/                progress, settings, session — never shipped or overwritten
    └── logs/                pocketchess.log, selftest.log
```

## Verified on hardware

| Date | Firmware | What | Result |
| --- | --- | --- | --- |
| — | 6.10 | App launches, board renders, taps select squares | not yet run (M0.4) |
| — | 6.10 | Self-test (45 suites incl. perft and golden checksums) | not yet run |
| — | 6.10 | Diagnostics: canvas format, update latencies, key codes, lifecycle | not yet run (M0.5) |
| — | 6.10 | Stockfish readyok / RSS / nps | not yet run (M0.6) |
