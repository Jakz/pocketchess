# ADR 0006 — The Process interface lives in core/engine

- Status: Accepted (2026-09-28). Amends the file list in architecture doc §2 (`platform/Process.h`).

## Decision

`engine::Process` (spawn, writeLine, terminate, setLowPriority, stats) is declared in
`src/core/engine/Process.h` as a pure interface with a static `spawn()` whose implementation is
chosen at link time: `src/platform/win32/Win32Process.cpp` (CreateProcessW, pipes, kill-on-close
job object, GetProcessTimes/GetProcessMemoryInfo) or `src/platform/posix/PosixProcess.cpp`
(pipe2/fork/execv, setpriority, SIGKILL after grace, wait4 rusage for CPU time and peak RSS).

## Why

The engine layer in core is written against the abstract process (architecture doc §1); keeping
the header in core avoids a core → platform include while still keeping all OS code in
`src/platform`. `Host`, `Presenter`, `TextInput` and `Power` stay in `src/platform/Host.h`
(used by `app`).

## Testing

The test executable doubles as a scripted UCI engine (`--fake-engine`), so the real process code
is tested on Windows, Linux and the reader (self-test spawns itself via `/proc/self/exe`).
