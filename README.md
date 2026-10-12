# PocketChess

An offline chess study app for **PocketBook** e-readers, made for the **Verse Pro Color**
(PB634K3, firmware 6.10). It plays, trains and teaches without a network connection, with
Stockfish running on the reader, and it is designed for e-ink: no animations, minimal screen
updates, and controls at the bottom for one-hand use. GPL-3.0-or-later.

![Home, Puzzles, Games study, Guess the move, Analyze, Stats](docs/images/overview.png)

*Screens as the reader shows them: the images are the app's real canvas output (golden test images).*

## What it does

- **Play** against Stockfish at five strengths. Several games can be in progress at once, saved
  after every move, with PGN export.
- **Analyze** any position or game: up to three engine lines, variations, comments, opening names
  and the opening statistics of your game packs.
- **Puzzles** from the Lichess database (50k bundled, up to all 6 million with the setup tool):
  - a rating, and spaced review of the puzzles you failed;
  - **My mistakes**: the bad moves of your own games turned into puzzles;
  - **Endgames**: ten classic positions played out against the engine.
- **Games**: browse and study historical games (filters by player, year, result, opening;
  favorites), your own games, and **Guess the move** to play through a master game as one side.
- **Openings**: about 3,800 named lines, and opening practice against the moves masters chose.
- **Stats** for every mode, kept on the reader, with backup and restore over USB.
- Color and black-and-white themes, four piece sets, import of PGN/FEN files, packs, themes, piece
  sets and engines through an `imports/` folder.

## Install on your reader

Download the package for your computer from the
[latest release](https://github.com/Jakz/pocketchess/releases/latest):

| Computer | Package |
| --- | --- |
| Windows 10/11 | `PocketChess-setup-windows-x64.zip` |
| macOS, Apple silicon | `PocketChess-setup-macos-arm64.zip` |
| macOS, Intel | `PocketChess-setup-macos-x64.zip` |
| Linux x86-64 | `PocketChess-setup-linux-x64.zip` |

Unzip it, open a terminal in that folder, connect the reader over USB, and choose **"PC link"** on
its screen. Then:

```text
pocketchess-setup install
```

This copies PocketChess to the reader with its engines, the 50k puzzle pack and the annotated
Classics games, and gives it a home-screen icon. Your progress on the reader is never
overwritten by later installs. PocketChess then appears among the reader's applications.

Optional content, fetched on your computer (needs internet), then installed the same way:

```text
pocketchess-setup fetch --historical --puzzles 500k
pocketchess-setup install --group Games
```

- `--historical`: an 80 MB pack of historical games built from PGN Mentor's collections. They
  are free to download, but may not be redistributed, so they are not in the package.
- `--puzzles 50k|100k|500k|1m|all`: larger puzzle packs (CC0).
- `--group Games`: also moves PocketChess to that home-screen group.

The full guide covers macOS and Linux notes, choosing the reader, how to undo the icon, and
uninstalling: [`docs/installer.md`](docs/installer.md) (also `README.txt` in each package).

**Other PocketBook models** with firmware 6.x and a 1072×1448 screen should work but are untested.

## Using it

- The reader's **◀ ▶** page buttons do what the ◀ ▶ buttons at the bottom of each screen show
  (move, page, hint …), and **Menu** opens the screen's menu. Every screen can be used with the
  three keys alone.
- `imports/` and `exports/` in `applications/pocketchess/` on the reader exchange files over USB:
  PGN files, packs, themes, piece sets and engines in; exported games and progress backups out.
- Settings → Display: color theme, accent color, piece colors, piece set, black-and-white mode.

## Build from source

The app is developed in parallel for **Windows** (Visual Studio, with an e-ink simulator) and the
**reader** (CMake + Ninja + Zig cross-compile). Contributor conventions: [`AGENTS.md`](AGENTS.md);
design decisions: [`docs/adr/`](docs/adr/); state of the work: [`docs/project-status.md`](docs/project-status.md).

### Requirements

- Windows with Visual Studio 2026 (C++ workload, toolset v145, "C++ CMake tools for Windows").
- **SDL 3.4.16** sources in `libs/sdl3/` (`include/` and `src/` of the
  [release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)). `scripts/vendor-libs.ps1`
  copies them from a sibling checkout if you have one.
- For the reader and the cross-platform tools, a toolchain folder with:
  - [Zig 0.16.0](https://ziglang.org/download/) as `zig/0.16.0/zig.exe`;
  - the InkView 6.10 headers and `libinkview.so` as `inkview/6.10/`, from
    [koreader/pocketbook-inkview](https://github.com/koreader/pocketbook-inkview/tree/master/610).

  Pass its path with `-ToolchainRoot` or set `POCKETBOOK_TOOLCHAIN_ROOT`
  ([details](projects/pocketbook/README.md)).
- Engines: `scripts/fetch-engines.ps1` downloads the pinned Stockfish sources and network into
  `libs/` (not in Git). Builds themselves never download anything.

### Build, test, package

| What | How |
| --- | --- |
| Desktop app with the e-ink simulator | `projects\msvc\PocketChess.sln`, configuration **Desktop**, F5 |
| Tests (~170 suites, golden images) | `projects\msvc\build.ps1 -Target Test` |
| Reader app, self-test and engines | `projects\pocketbook\build-zig.ps1 -Configuration Release` |
| Setup tool and importers for 4 platforms | `projects\tools\build-tools.ps1` |
| Release packages | `build-zig.ps1 -Configuration Release`, then `projects\tools\build-tools.ps1 -Package` → `out\installer\` |
| Content packs on your PC | `scripts\fetch-lichess.ps1`, `scripts\fetch-historical.ps1`, `scripts\build-content.ps1` |
| Copy a development build to the reader | `projects\pocketbook\deploy.ps1 -WithContent -Eject` |
| All checks | `scripts\verify-all.ps1` |

Desktop keys: PgUp/PgDn or ←/→ = Prev/Next, M = Menu, Esc = Back, Enter = OK, right-click = long
press. Simulator: F2 display mode (exact / 16 grays / Kaleido 3 color), F3 refresh rectangles,
F4 full-refresh flash, F6 latency emulation, F9 sleep/wake, F12 screenshot, Ctrl+wheel zoom.

### Layout

```text
src/core        chess rules, PGN, engine protocol, storage, puzzles, games, openings, drills
src/ui          software canvas, TrueType rasterizer, board view, themes, widgets, refresh policy
src/app         application and screens, dialogs, scripting, headless host
src/platform    SDL desktop + e-ink simulator, PocketBook (InkView), process spawning
src/tools       PgnImporter, PuzzleImporter, pocketchess-setup
tests           core/ui/app/tools suites, golden images, fake UCI engine
projects        msvc (solution), pocketbook (CMake + Zig), tools (cross builds)
data            fonts, piece sets, themes, openings, endgames, icons, samples
docs            design notes, plan (docs/ai), ADRs, installer guide, licensing
```

## Licences

PocketChess is GPL-3.0-or-later ([`LICENSE`](LICENSE)). It ships Stockfish (GPL-3.0), SQLite,
nanosvg, DejaVu fonts, the Cburnett/Merida/Chessnut piece sets, and Lichess puzzle and
opening data (CC0): see [`THIRD-PARTY-NOTICES.txt`](THIRD-PARTY-NOTICES.txt) and
[`docs/licensing.md`](docs/licensing.md). PocketChess is not affiliated with PocketBook,
Stockfish or Lichess.
