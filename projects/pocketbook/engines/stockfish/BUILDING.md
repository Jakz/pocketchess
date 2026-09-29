# Stockfish plug-in — build notes

Status: sources fetched and pinned (sf_19, 2026-09-29). The CMake build was checked against the
real sources on Linux x86-64 (clang, `x86-64-avx2` flags): `bench` and the `[integration]` tests
pass. The Zig build for `windows-x64` works on Windows (2026-09-29: `bench`, `[integration]` tests,
RSS 117 MiB after a depth-19 search with the net beside the binary). The `arm` build links (static musl hard-float, ADR 0009) and
still needs its first run on the reader.

Zig's clang rejects `__DATE__` (`-Werror=date-time`), which `misc.cpp` uses unless `GIT_DATE` is
defined; `CMakeLists.txt` defines a fixed `GIT_DATE` (release builds never print it).

Stockfish 19 facts that matter here: one network, `nn-1a298aa575a0.nnue` (98.5 MB); threads are
pthreads with every compiler but MSVC (`thread_native.h`); `UCI_Elo` spans 1320–3190 (P10
confirmed); the loaded weights go to shared memory when the OS allows it ("Network replica 1:
Shared memory"), otherwise to the heap — check which one the reader uses.

## Net beside the binary (default since 2026-09-29, R2)

`PCHESS_SF_EMBED_NET` is OFF: the binary is ~1 MB and `nn-*.nnue` ships beside it (listed in
`files:` of `data/engines/stockfish/engine.yaml`, which `CMakeLists.txt` checks against the pin).
Stockfish looks for the net in its working directory and then in its binary's folder, so no
`EvalFile` option is sent. Measured on Windows x64 (Hash 16, 3 s search):

| Build | After readyok | Peak working set | Private | readyok | bench nodes/s |
| --- | --- | --- | --- | --- | --- |
| net embedded | 211 MiB | 321 MiB | 154 MiB | ~178 ms | ~1.63 M |
| net beside the binary | 118 MiB | 228 MiB | 60 MiB | ~213 ms | ~1.62 M (noise) |

The embedded copy stays resident next to the loaded weights; `bench` node counts are identical.

## Pin

`engine.lock` in this folder records the upstream tag, the source archive URL and SHA-256, and
the network download base. Network files are verified by their name: Stockfish names each net
`nn-<first 12 hex digits of its SHA-256>.nnue`, and `fetch-engines.ps1` checks that prefix.

## Toolchains

- `arm`: `../zig-arm-engine.cmake` with the `zig-arm-*.cmd` wrappers (`arm-linux-musleabihf`,
  `-mcpu=cortex_a7`, static), not the app's soft-float `ZigToolchain.cmake` (ADR 0009).
- `windows-x64`: `../zig-windows-x64.cmake` with the `zig-win-*.cmd` wrappers
  (`x86_64-windows-gnu`). Both need `ZIG_EXE`, set by `build-engine.ps1`, which finds
  `zig\<version>\zig.exe` in the toolchain and cmake/ninja in Visual Studio's "C++ CMake tools".

## Flags replicated from the upstream Makefile

| Target | Makefile ARCH | Defines | Compiler flags |
| --- | --- | --- | --- |
| `arm` (PB634K3, Cortex-A7) | `armv7-neon` | `USE_POPCNT USE_NEON=7 NDEBUG` | `-mcpu=cortex_a7 -mfpu=neon` (hard-float, static musl); links `pthread` |
| `windows-x64` | `x86-64-avx2` | `IS_64BIT USE_POPCNT USE_AVX2 USE_SSE41 USE_SSSE3 USE_SSE2 NDEBUG` | `-mavx2 -mbmi -mpopcnt -msse4.1 -mssse3 -msse2`; links `pthread` (winpthreads), `advapi32`, `-static` |

To do when vendoring (M0.6):

1. When the pin changes: open `libs/stockfish/src/Makefile`, compare the `armv7-neon` /
   `x86-64-avx2` settings and the generic `CXXFLAGS`/`LDFLAGS` with the table (done for sf_19).
   `src/universal/` (multi-arch builds) is not compiled.
2. Embedding (`-DPCHESS_SF_EMBED_NET=ON`, not the default): `CMakeLists.txt` copies `nn-*.nnue`
   into the build folder, which is the compiler's working directory under Ninja, so INCBIN finds
   them (the Makefile compiles in `src/`). Then set `files: []` in the descriptor.
3. Windows smoke test: `stockfish.exe bench 16 1 8` must print `Nodes searched` (build-engine.ps1
   does this automatically).
4. Device: Diagnostics → Engine records time to `readyok`, RSS, nodes/s and CPU-seconds; copy the
   log lines into `docs/project-status.md` and the `resources:` block of the descriptor (D4
   checkpoint: readyok < 3 s and RSS < 250 MB).
