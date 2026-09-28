# PocketChess — implementation plan (AI handoff)

Last updated: 2026-09-28.

This folder turns `docs/chess_study_pocketbook_design_notes.md` (the design notes) into an
implementation plan. The design notes remain the product specification. These documents
record **how** the product is built and **what changed** from the notes.

| File | Content |
| --- | --- |
| [01-decisions.md](01-decisions.md) | Agreed decisions (D1–D15) and deviations from the design notes |
| [02-repository-and-build.md](02-repository-and-build.md) | Folder layout, `libs/`, MSVC solution, PocketBook Zig build, bundle and deploy, parity rules |
| [03-architecture.md](03-architecture.md) | Module boundaries, platform layer, software canvas, refresh, input, engine, storage, device file layout |
| [04-milestones.md](04-milestones.md) | Milestones M0–M6 in detail (tasks, deliverables, acceptance criteria), plus the M7+ backlog |
| [05-testing-and-verification.md](05-testing-and-verification.md) | Test strategy, the Windows e-ink simulator, device smoke checklists |
| [06-risks-and-open-questions.md](06-risks-and-open-questions.md) | Risks, spikes, and questions still open |

## One-paragraph summary

PocketChess is a C++20 offline chess study app for the **PocketBook Verse Pro Color (PB634K3,
firmware 6.10, 1072×1448, Kaleido 3)**. It is built **in parallel** as:

1. a **Windows desktop build**: native Visual Studio 2026 projects in `projects/msvc/` with an
   SDL3 e-ink simulator window, used for day-to-day development and debugging (F5);
2. a **PocketBook build**: CMake + Ninja + the shared **Zig 0.16.0** toolchain in
   `C:\Users\jack\Documents\Dev\libs\pocketbook`, driven by `projects/pocketbook/build-zig.ps1`,
   producing `pocketchess.app` and a ready-to-copy bundle.

Both builds compile the **same portable sources** (`src/core`, `src/ui`, `src/app`). Only
`src/platform/<backend>` differs. All drawing goes through a platform-independent **software
canvas**, so the Windows window shows exactly the pixels the reader receives. **Stockfish
(latest release, NNUE)** runs as a child process over UCI. It is cross-compiled with the same
Zig toolchain for ARM, and also built for Windows x64. Engines are **plug-ins** described by YAML
manifests, and **power profiles** (Performance / Balanced / Eco) choose the engine and search budgets,
so a lighter energy-saver engine can be added or swapped later. Puzzle packs, game packs and piece sets
are also swappable units with size/RAM budgets, so they can be optimized once there is device data.
The app and its data live in `/mnt/ext1/applications/` (`pocketchess.app` + `pocketchess/`). The desktop **importers** (Lichess
puzzles, PGN, piece-set assets) are C++ tools in the same solution and reuse the chess core.

## Reading order for a new agent/session

1. Design notes (product intent).
2. `01-decisions.md` (what differs from the notes).
3. `02-repository-and-build.md` + `03-architecture.md`.
4. The current milestone in `04-milestones.md`.
5. Root `AGENTS.md` (created in M0) for coding conventions.

The Enigmistica repository (`..\enigmistica`) is the reference for the harness:
`projects/pocketbook/build-zig.ps1`, the `InkViewRenderer`/`SdlRenderer` split, `DirtyRegions`, and the
native MSBuild `.props` pattern. Copy from it and adapt; do not link to it.
