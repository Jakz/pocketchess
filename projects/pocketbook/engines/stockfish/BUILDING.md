# Stockfish plug-in — build notes

Status: **scaffolded, not yet built** (M0.6). Everything here must be checked against the vendored
sources the first time `scripts/fetch-engines.ps1 -Engine stockfish` runs.

## Pin

`engine.lock` in this folder records the upstream tag, the source archive URL and SHA-256, and
the network download base. Network files are verified by their name: Stockfish names each net
`nn-<first 12 hex digits of its SHA-256>.nnue`, and `fetch-engines.ps1` checks that prefix.

## Toolchains

- `arm`: the shared `ZigToolchain.cmake` (`zig-cc.cmd`/`zig-cxx.cmd`, `arm-linux-gnueabi.2.36`).
- `windows-x64`: `../zig-windows-x64.cmake` with the `zig-win-*.cmd` wrappers
  (`x86_64-windows-gnu`). Both need `ZIG_EXE`, set by `build-engine.ps1`, which finds
  `zig\<version>\zig.exe` in the toolchain and cmake/ninja in Visual Studio's "C++ CMake tools".

## Flags replicated from the upstream Makefile

| Target | Makefile ARCH | Defines | Compiler flags |
| --- | --- | --- | --- |
| `arm` (PB634K3, Cortex-A7) | `armv7-neon` | `USE_POPCNT USE_NEON=7 USE_PTHREADS NDEBUG` | `-mcpu=cortex_a7 -mfpu=neon -mfloat-abi=softfp` |
| `windows-x64` | `x86-64-avx2` | `IS_64BIT USE_POPCNT USE_AVX2 USE_SSE41 USE_SSSE3 USE_SSE2 NDEBUG` (no `USE_PTHREADS`: the Makefile omits it for mingw) | `-mavx2 -mbmi -mpopcnt -msse4.1 -mssse3 -msse2`; links `advapi32`, `-static` |

To do when vendoring (M0.6):

1. Open `libs/stockfish/src/Makefile`, find the `armv7-neon` and `x86-64-avx2` blocks and the
   generic `CXXFLAGS` section, and compare with the table (Stockfish 19 may have renamed flags
   or added `USE_NEON_DOTPROD`/`-DARCH=...`). Update `CMakeLists.txt` and this table.
2. Embedding: `CMakeLists.txt` copies `nn-*.nnue` into the build folder, which is the
   compiler's working directory under Ninja, so INCBIN finds them (the Makefile compiles in `src/`).
   If the ARM build fails at the assembler, configure with `-DPCHESS_SF_EMBED_NET=OFF`
   (`NNUE_EMBEDDING_OFF`, net shipped beside the binary, `setoption name EvalFile`) and set
   `files:` in `data/engines/stockfish/engine.yaml` (R2).
3. Windows smoke test: `stockfish.exe bench 16 1 8` must print `Nodes searched` (build-engine.ps1
   does this automatically).
4. Device: Diagnostics → Engine records time to `readyok`, RSS, nodes/s and CPU-seconds; copy the
   log lines into `docs/project-status.md` and the `resources:` block of the descriptor (D4
   checkpoint: readyok < 3 s and RSS < 250 MB).
