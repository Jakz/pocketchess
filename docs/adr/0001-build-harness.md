# ADR 0001 — Two build harnesses from one source tree

- Status: Accepted (2026-09-28), implements D1, D2, D3, D13
- Context: the plan requires a debuggable Windows build and a PocketBook cross-build in parallel.

## Decision

- Windows: native MSBuild projects in `projects/msvc/` (`PchessCore`, `PchessUi`,
  `PchessPlatformDesktop`, `PocketChess`, `PocketChessTests`, `PocketChessHeadless`, `SDL3`) plus
  two Makefile projects: `PocketBook` (calls `build-zig.ps1`) and `Engines` (calls
  `build-engine.ps1 -Target windows-x64 -AllowMissing`). Configurations Debug/Release build all;
  Desktop skips the Makefile projects.
- PocketBook: `projects/pocketbook/CMakeLists.txt` (refuses to configure natively) + `sources.cmake`.
  Platform sources are an OBJECT library because core's engine code calls `Process::spawn`, which
  lives in `PosixProcess.cpp` (a static archive scanned before `pchess-core` would miss it).
- `scripts/check-sources.ps1` (pre-build step of `PchessCore`) compares `sources.cmake` with the
  `.vcxproj` items, requires every `.cpp` to be listed and enforces the no-platform-headers rule.
- Both bundles are staged by `scripts/stage-bundle.ps1` from `projects/bundle-manifest.txt`.
- Toolchain lookup: `-ToolchainRoot`, `%POCKETBOOK_TOOLCHAIN_ROOT%`, `..\..\libs\pocketbook`.

## Consequences

- `build-zig.ps1` was written from the plan, not copied from Enigmistica (that repo could not be
  read in the implementing session): the ZigToolchain.cmake variables it passes (`ZIG_EXE`, cache
  dirs, `POCKETBOOK_TOOLCHAIN_ROOT`) must be checked against Enigmistica's script on first run.
- The CMake project was validated on Linux with a stub toolchain and stub InkView (build, link,
  self-test run); the real Zig/InkView build is the first M0.4 task.
