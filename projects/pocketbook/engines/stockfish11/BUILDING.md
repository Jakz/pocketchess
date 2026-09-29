# Stockfish 11 plug-in (Eco candidate) — build notes

Status: sources fetched and pinned (sf_11, 2026-09-29). Zig builds for `windows-x64` (`bench 16 1 8`
→ 379601 nodes) and `arm` (static musl hard-float, ADR 0009) link; the device run is pending.

Stockfish 11 is the last release with the classical evaluation only: no network files, a ~0.7 MB
binary, `UCI_Elo` 1350–2850, `Skill Level` 0–20. Threads are pthreads on mingw and Linux
(`thread_win32_osx.h`).

## Flags replicated from the upstream Makefile

| Target | Makefile ARCH | Defines | Compiler flags |
| --- | --- | --- | --- |
| `arm` (PB634K3, Cortex-A7) | `armv7` | `NDEBUG` (32-bit, prefetch, no popcnt) | toolchain `-mcpu=cortex_a7`; links `pthread`, static |
| `windows-x64` | `x86-64-modern` | `IS_64BIT USE_POPCNT NDEBUG` | `-msse -msse3 -mpopcnt`; links `pthread` (winpthreads), `-static` |

`misc.cpp` always expands `__DATE__` (for dev versions only), so the build passes `-Wno-date-time`
(Zig's clang makes it an error). Upstream compiles with `-std=c++11`; so does `CMakeLists.txt`.
