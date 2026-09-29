# ADR 0009 — Device engines are static arm-linux-musleabihf binaries

- Status: Accepted (2026-09-29). Refines D4/D8 (engine plug-ins) and build doc §4.3.

## Decision

Engine plug-ins for the reader are built by `projects/pocketbook/build-engine.ps1 -Target arm` with
their own toolchain, `projects/pocketbook/engines/zig-arm-engine.cmake`: Zig's
`arm-linux-musleabihf` target, `-mcpu=cortex_a7`, fully static (`-static`) and stripped (`-s`).
The app itself keeps the shared `ZigToolchain.cmake` (`arm-linux-gnueabi.2.36`, softfp), because it
links against the device's `libinkview.so` and glibc.

## Why

- Zig's `arm-linux-gnueabi` target is soft-float: it disables the FP/NEON registers, whatever
  `-mfpu=neon -mfloat-abi=softfp` says. Stockfish 19's NEON NNUE code (`USE_NEON=7`) then aborts
  LLVM 21 ("Do not know how to split the result of this operator!") in `nnue_accumulator.cpp` and
  `network.cpp`. Building without NEON would make the NNUE evaluation several times slower on the
  Cortex-A7, which matters for both the ADR 0004 checkpoint and battery use.
- The float ABI only governs calls between objects; an engine is a separate process that talks UCI
  over pipes. A static binary has no shared libraries, so hard-float inside it cannot clash with the
  device's softfp glibc, and the kernel runs it as any other ARM EABI5 executable.
- Static musl also removes the glibc-version question (R5) for engines: no `GLIBC_2.x` symbols.

## Consequences

- `check-elf.ps1` (soft-float flag, allowed NEEDED list) applies to the app, not to engines; an
  engine check is: ELF32 ARM, statically linked, no `PT_INTERP`.
- musl's allocator and threads differ from glibc's; the device benchmark (Diagnostics → Engines)
  measures the result. If nodes/s disappoints, the fallback is `arm-linux-gnueabihf` + static glibc.
- Stockfish 19 for arm is ~1 MB plus its 98.5 MB net, shipped beside the binary (not embedded:
  ~95 MB less RSS at the same speed, see `engines/stockfish/BUILDING.md`).
- Stockfish 19 keeps its weights in shared memory where available (`shm_open` on Linux, via musl);
  if the reader has no usable `/dev/shm`, it should fall back to the heap. Check its log line.
- To verify on the device (M2 checklist): the engine starts from `applications/pocketchess/engines/`
  (exec allowed, M0.5), reaches `readyok`, and `bench` matches the desktop node count (170203 for
  `bench 16 1 8`), which also proves the NEON code paths are correct.
