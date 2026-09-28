# 02 — Repository layout and build harness

## 1. Folder layout

```text
pbchess/
├── AGENTS.md                      # conventions for humans/agents (created in M0)
├── README.md                      # how to build/run/deploy (created in M0)
├── LICENSE                        # GPL-3.0-or-later (D15)
├── .gitignore  .gitattributes  .editorconfig  .clang-format
│
├── docs/
│   ├── chess_study_pocketbook_design_notes.md   # product spec (existing)
│   ├── ai/                        # this plan
│   ├── adr/                       # short decision records (0001-…)
│   └── project-status.md          # handoff/resume point, updated each milestone
│
├── src/
│   ├── core/                      # platform-free, no UI, no SDL/InkView
│   │   ├── chess/                 # Bitboard, Square, Move, Position, MoveGen, Fen, San, Pgn,
│   │   │                          # UciMove, MoveCodec, Zobrist, GameTree
│   │   ├── engine/                # UciParser, UciEngine, EngineManager, EngineJobs, Strength
│   │   ├── puzzles/               # Puzzle, PuzzleRepository, PuzzleSelector, PuzzleSession,
│   │   │                          # RatingService, ReviewScheduler
│   │   ├── games/                 # GameRecord, GameRepository, GameQuery, StudyCursor
│   │   ├── storage/               # Database (SQLite RAII), Statement, Migrations, Schemas
│   │   ├── settings/              # Settings model + persistence
│   │   └── util/                  # Log, Clock, Random, Paths, Utf8, Error
│   ├── ui/                        # platform-free presentation
│   │   ├── canvas/                # Canvas (RGB), Bitmap, Blend, FontRasterizer (stb_truetype)
│   │   ├── render/                # Geometry, DirtyRegions, RefreshPolicy, PresentRequest
│   │   ├── theme/                 # BoardTheme, PieceSet, ThemeLoader (yaml)
│   │   ├── widgets/               # Button, Label, ListView, MoveList, Dialog, TabBar, Keyboard
│   │   ├── views/                 # BoardView, HomeView, PlayView, AnalysisView, PuzzleView,
│   │   │                          # GameBrowserView, GameStudyView, SettingsView, ImportView
│   │   └── input/                 # InputEvent, KeyMap, GestureRecognizer (tap / long press)
│   ├── app/                       # Application, ScreenStack, controllers, SessionState
│   ├── platform/
│   │   ├── Host.h  Process.h  TextInput.h  Power.h   # interfaces
│   │   ├── sdl/                   # SdlHost, SdlPresenter, EinkSimulator, main (desktop)
│   │   ├── pocketbook/            # InkViewHost, InkViewPresenter, main (device)
│   │   ├── win32/                 # Win32Process (CreateProcess + pipes)
│   │   └── posix/                 # PosixProcess (fork/exec + pipes)
│   └── tools/
│       ├── puzzle_importer/       # Lichess CSV(.zst) -> puzzles.db
│       ├── pgn_importer/          # *.pgn -> games.db
│       ├── asset_converter/       # SVG piece sets -> sized PNG sets
│       └── common/                # CSV reader, zstd stream, progress output
│
├── tests/                         # doctest suites + fixtures (small, CC0/own)
│   ├── core/ ui/ app/ tools/
│   ├── golden/                    # golden canvas PNGs
│   └── fixtures/                  # perft.epd, sample.pgn, puzzles-sample.csv, uci transcripts
│
├── data/                          # source data/assets checked into Git
│   ├── engines/<id>/engine.yaml   # engine descriptors (D8)
│   ├── pieces/<set>/*.svg         # source SVG piece sets + LICENSE per set
│   ├── themes/*.yaml              # board themes (color + mono)
│   ├── fonts/                     # Noto Sans (+ licence)
│   ├── openings/                  # ECO/opening names TSV (lichess chess-openings, CC0)
│   ├── samples/                   # tiny PGN/puzzle packs for first run & tests
│   ├── downloads/                 # IGNORED: lichess_db_puzzle.csv.zst, big PGNs
│   └── generated/                 # IGNORED: importer outputs (puzzles.db, games.db, PNG sets)
│
├── libs/                          # pinned third-party snapshots (no submodules, no fetch)
│   ├── README.md                  # table: version, commit, licence, SHA-256, use
│   ├── sdl3/  sqlite/  stb/  yaml/  doctest/  nanosvg/  zstd/
│   ├── stockfish/  <eco-engine>/  # engine sources, one folder per engine (D8)
│
├── projects/
│   ├── msvc/                      # PocketChess.sln + native .vcxproj + props + build.ps1
│   ├── engines.txt                # engine ids built into the bundle (D8)
│   └── pocketbook/                # CMakeLists.txt, build-zig.ps1, build-engine.ps1, engines/<id>/,
│                                  # deploy.ps1, check-elf.ps1, README.md
│
├── scripts/                       # check-sources.ps1, verify-all.ps1, fetch-lichess.ps1,
│                                  # build-content.ps1
└── out/                           # IGNORED: all build outputs, caches, bundles
```

Conventions (carried over from Enigmistica): first-party C++ files are `CamelCase.cpp/.h`,
directories are lowercase, and every file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.
`src/core` and `src/ui` must never include `<SDL3/…>`, `<inkview.h>`, `<windows.h>` or POSIX headers.
A check in `check-sources.ps1` greps for this.

## 2. `libs/` (local pinned copies)

| Dir | Library | Used by | Notes |
| --- | --- | --- | --- |
| `sdl3/` | SDL 3.x (copy the pinned snapshot already in Enigmistica) | Windows app, desktop tools with a UI | Built by `SDL3.vcxproj`; reuse Enigmistica's `projects/importer/msvc/SDL3.vcxproj(.filters)` |
| `sqlite/` | SQLite amalgamation (latest 3.x) | core (both platforms), tools | Compile options: `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_DEFAULT_MEMSTATUS=0`, `SQLITE_THREADSAFE=1`, `SQLITE_DQS=0` |
| `stb/` | `stb_image.h`, `stb_truetype.h`, `stb_image_write.h` | ui/canvas, tests, tools | Copy the pinned headers from Enigmistica; add `stb_image_write.h` for screenshots and golden images |
| `yaml/` | yaml-cpp 0.9.0 (same snapshot as Enigmistica) | themes, content packs | Static, no tools/tests |
| `doctest/` | doctest single header | tests | |
| `nanosvg/` | nanosvg + nanosvgrast | asset_converter only | Desktop tool only |
| `zstd/` | zstd (decompression sources only) | puzzle_importer only | Desktop tool only; never in the device build |
| `stockfish/` | Stockfish 19 `src/` + the `.nnue` file(s) named in `evaluate.h` | engine binaries | **Local only, not in Git** (see below). GPL-3; keep `Copying.txt` and `AUTHORS` |
| `<eco-engine>/` | Energy-saver engine chosen in M2 (candidates in architecture doc §8.4) | engine binaries | **Local only, not in Git**. Only if it wins the M2 benchmark; licence must be GPL-3 compatible |

`libs/README.md` records the upstream URL, tag, commit, licence and SHA-256 of single files,
following Enigmistica's table. Builds never download anything. Updating a library means replacing the
snapshot, updating the table, and rebuilding **both** targets.

**Engine sources are local-only (for now).** Engine source trees and networks are large, so they stay
out of the GitHub repo. `.gitignore` excludes `libs/stockfish*/` and any other engine folder listed in
`projects/engines.txt`. What **is** committed for each engine:

- `projects/engines.txt`, plus a pin file `projects/pocketbook/engines/<id>/engine.lock` (upstream URL, tag,
  commit, network file names and SHA-256);
- the build files `projects/pocketbook/engines/<id>/CMakeLists.txt` and `BUILDING.md` (the replicated
  Makefile flags);
- the descriptor `data/engines/<id>/engine.yaml`.

`scripts/fetch-engines.ps1` restores the local copies from the pins (download the tag's source
archive and the network files, verify SHA-256, and unpack into `libs/<id>/`). After builds, it is
the only script besides `fetch-lichess.ps1` that touches the network. Builds never call it. When
`libs/<id>/` is missing, `build-engine.ps1` stops with a message that points to it. On this PC, the
local copies simply stay in place. Moving engines into the repo later (as vendored source or a
submodule) changes only `.gitignore` and this section.

## 3. Windows build: `projects/msvc/`

### 3.1 Solution contents (`PocketChess.sln`, VS 2026, toolset v145, x64)

| Project | Type | Content |
| --- | --- | --- |
| `Common.props` | props | `RepoRoot`, `OutDir=out\msvc\$(Configuration)\`, `IntDir=out\msvc\obj\$(ProjectName)\$(Configuration)\`, C++20, `/utf-8 /permissive- /W4 /Zc:__cplusplus`, `NOMINMAX`, include dirs `src`, `libs\…` |
| `Sqlite.vcxproj` | static lib | `libs/sqlite/sqlite3.c` (C, `/W0`) |
| `YamlCpp.vcxproj` | static lib | copy from Enigmistica |
| `SDL3.vcxproj` | static lib | copy from Enigmistica |
| `PchessCore.vcxproj` | static lib | `src/core/**` |
| `PchessUi.vcxproj` | static lib | `src/ui/**`, `src/app/**` |
| `PchessPlatformDesktop.vcxproj` | static lib | `src/platform/sdl/**`, `src/platform/win32/**` (except `Main.cpp`) |
| **`PocketChess.vcxproj`** | application (startup) | `src/platform/sdl/Main.cpp`; post-build stages `out\msvc\<cfg>\pocketchess\` (§5) |
| `PocketChessTests.vcxproj` | console app | `tests/**`; post-build runs the tests (can be disabled with the property `RunTestsAfterBuild=false`) |
| `ToolsCommon.vcxproj`, `PuzzleImporter.vcxproj`, `PgnImporter.vcxproj`, `AssetConverter.vcxproj`, `Zstd.vcxproj` | libs/console apps | Desktop tools (D5) |
| `Engines.vcxproj` | Makefile project | Calls `projects/pocketbook/build-engine.ps1 -Target windows-x64` for every engine in `projects/engines.txt`. Output: `out\engines\<id>\windows-x64\` |
| `PocketBook.vcxproj` | Makefile project | Build/Rebuild/Clean → `projects/pocketbook/build-zig.ps1 -Configuration $(Configuration)`. NMake include paths point to `src` and the InkView headers, so IntelliSense works in `src/platform/pocketbook`. |

Solution configurations: `Debug|x64` and `Release|x64` build everything, including `PocketBook`
and `Engines`. A third configuration, **`Desktop|x64`** (a copy of Debug without the Makefile projects),
keeps the edit–F5 loop fast when the ARM build is not needed. Files that belong only to the other
platform (`src/platform/pocketbook/*`, `src/platform/posix/*`) are listed in `PocketBook.vcxproj`
so they are visible and searchable in Solution Explorer.

### 3.2 Debugging defaults

- `PocketChess.vcxproj.user` is not committed. Instead, `LocalDebuggerWorkingDirectory=$(OutDir)`
  and `LocalDebuggerCommandArguments=--scale 0.6` are set in `Common.props`, so F5 works from a fresh
  checkout (the Enigmistica lesson: the app finds its data beside the executable, not in the CWD).
- Command-line options of the desktop app (also used by tests and screenshots):
  `--scale <f>`, `--profile verse-pro-color|mono-758x1024|…`, `--root <dir>`,
  `--screen <home|play|tactics|games|analyze>`, `--fen "<fen>"`,
  `--screenshot <out.png>` (render one frame and exit), `--replay <input.log>`, `--mono`.

### 3.3 `projects/msvc/build.ps1`

This is a helper for command-line/CI use. It is not needed inside Visual Studio.
`-Configuration Debug|Release|Desktop`, `-Target Build|Rebuild|Clean|Test`. It locates MSBuild with
`vswhere`, the way Enigmistica's `projects/importer/msvc/build.ps1` does.

## 4. PocketBook build: `projects/pocketbook/`

### 4.1 `CMakeLists.txt` (lives here, not at the repo root)

- Refuses to configure when `NOT CMAKE_CROSSCOMPILING` (same guard as Enigmistica).
- Takes `POCKETBOOK_TOOLCHAIN_ROOT` and `POCKETBOOK_SDK_ROOT` (default `<toolchain>/inkview/6.10`),
  and finds `inkview.h` + `libinkview.so` with `NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH`.
- Includes `sources.cmake` (in the same folder), which defines `PCHESS_CORE_SOURCES`,
  `PCHESS_UI_SOURCES`, `PCHESS_PLATFORM_PB_SOURCES` and `PCHESS_TEST_SOURCES` with paths relative to
  `${REPO_ROOT}`. **`check-sources.ps1` compares these lists with the `ClCompile` items of
  `PchessCore/PchessUi/PocketChessTests.vcxproj`** and fails on any difference.
- Targets:
  - `pchess-sqlite`, `pchess-yaml`: static libraries from `libs/`;
  - `pchess-core`, `pchess-ui`: static libraries;
  - `pchess-pocketbook` → `pocketchess.app` (`OUTPUT_NAME pocketchess`, `SUFFIX .app`), linking
    `libinkview.so`. The compat include handling is copied from Enigmistica: `compat/zlib.h` when the
    SDK has no `zlib.h`, and `-idirafter <sdk>/../include[/freetype2]`;
  - `pchess-selftest` → `pocketchess-selftest.app`: the doctest suites plus a tiny InkView front
    end that writes results to `logs/selftest.log` and shows pass/fail on screen.
- Flags: `-O2` (Release) / `-O0 -g` (Debug), `-Wall -Wextra`,
  `-fsigned-char` **not** used (the code must be correct with unsigned `char`, which the self-test
  checks), and `-ffunction-sections -fdata-sections -Wl,--gc-sections` in Release.
- Zig statically links its own libc++ into C++ executables, so the device's `libstdc++` is not used.
  The rule is that **no C++ types cross the InkView boundary** (InkView is a C API).

### 4.2 `build-zig.ps1`

This is Enigmistica's script adapted. Parameters: `-Configuration Debug|Release`, `-ToolchainRoot`,
`-SdkLocal`, `-ZigPath`, `-SkipEngines`, `-Content <path-to-generated-dbs>`. Steps:

1. Resolve the toolchain (D3), Zig, and CMake/Ninja (PATH or VS 2026 bundled CMake). Set `ZIG_EXE`,
   `ZIG_GLOBAL_CACHE_DIR` and `ZIG_LOCAL_CACHE_DIR` under `out/pocketbook-zig/`.
2. `cmake -S projects/pocketbook -B out/pocketbook-zig/<cfg> -G Ninja -DCMAKE_TOOLCHAIN_FILE=<toolchain>/ZigToolchain.cmake …`.
3. Build `pchess-pocketbook` and `pchess-selftest`.
4. Run `check-elf.ps1` on each output: ELF32, `e_machine == 40` (ARM), soft-float ABI flag set,
   hard-float flag clear (the byte checks from Enigmistica). When `llvm-readelf` is available (VS LLVM
   component), also list `NEEDED` entries (only `libinkview.so`, `libc.so.6`, `libm.so.6`,
   `libpthread.so.0` and `ld-linux.so.3` are allowed) and the highest `GLIBC_2.x` symbol version (must be
   ≤ the device's glibc, which is recorded in M0).
5. Unless `-SkipEngines` is given, call `build-engine.ps1 -Target arm` for every engine listed in
   `projects/engines.txt` (incremental).
6. Stage the bundle (§5) into `out/pocketbook-zig/<cfg>/bundle/`.

### 4.3 `build-engine.ps1` (engines are plug-ins, D8)

- `-Engine <id>` (default: all in `projects/engines.txt`), `-Target arm|windows-x64`. Each engine has its
  sources in `libs/<id>/` (for example `libs/stockfish/`) and a small CMake project in
  `projects/pocketbook/engines/<id>/CMakeLists.txt`. The descriptor `data/engines/<id>/engine.yaml`
  (architecture doc §8.1) is staged beside the binary. Adding an engine means adding those three things
  and one line in `engines.txt`; no app code changes.
- Zig is used for both targets (`arm-linux-gnueabi.2.36` with
  `-mcpu=cortex_a7 -mfpu=neon -mfloat-abi=softfp`, and `x86_64-windows-gnu`).
- Stockfish specifics follow. Other engines get their own notes in `libs/<id>/BUILDING.md`.
- It replicates the preprocessor flags that the upstream `Makefile` uses for `ARCH=armv7-neon` and
  `ARCH=x86-64-avx2` (or the universal/native equivalent in SF 19). These flags are read from the vendored
  Makefile when vendoring and written down in `projects/pocketbook/engines/stockfish/BUILDING.md`
  (committed; the sources themselves are local-only, §2).
- The network is embedded with Stockfish's INCBIN mechanism, which works with Zig's clang. If
  embedding fails on ARM, set `NNUE_EMBEDDING_OFF` and ship the `.nnue` beside the binary, passing
  `setoption name EvalFile`.
- Outputs `out/engines/<id>/<target>/<binary>` and runs a smoke test on Windows (for Stockfish:
  `stockfish.exe bench 16 1 8` must finish and print a node count). ARM binaries are benchmarked on the
  device by the Diagnostics engine benchmark (M0.6, M2).

### 4.4 `deploy.ps1`

`-Drive E:` (auto-detected from a volume that contains `applications\`), `-Configuration`,
`-WithContent`, and `-Engines <ids>` (default: the engines in the bundle). It copies `pocketchess.app`
to `\applications\` and mirrors `bundle/pocketchess/` to `\applications\pocketchess\` **without touching
`user/`** (progress is never overwritten, design notes §24.1). It prints the copied sizes against the
storage budget (architecture doc §13) and ejects the volume when `-Eject` is passed.

### 4.5 `projects/pocketbook/README.md`

Same structure as Enigmistica's: prerequisites, the one-line build, the bundle layout, the copy
instructions, and a table of **what has been verified on hardware**, with dates.

## 5. Bundle / staged layout (identical on both platforms, D11)

```text
<root>/                                  Windows: out\msvc\<cfg>\pocketchess\
                                         Device : /mnt/ext1/applications/pocketchess/
├── engines/
│   ├── stockfish/engine.yaml + stockfish[.exe] (+ .nnue if not embedded)
│   └── <eco-engine>/engine.yaml + binary       # optional, see architecture doc §8
├── assets/
│   ├── pieces/<set>/<px>/{w,b}{k,q,r,b,n,p}.png   # precomputed sizes (design notes §18.1)
│   ├── themes/*.yaml
│   └── fonts/NotoSans-Regular.ttf (+ Bold)
├── content/
│   ├── puzzles/*.db                         # one or more puzzle packs (Starter/Standard/…)
│   └── games/*.db                           # one or more game packs
├── imports/                                 # user drops PGN/FEN/packs here (USB)
├── exports/                                 # PGNs saved by the app (USB-readable)
├── user/                                    # never shipped, never overwritten
│   ├── chess.db  progress.db  session.yaml
└── logs/pocketchess.log  logs/selftest.log
```

Device executable: `/mnt/ext1/applications/pocketchess.app`, with its data folder beside it (the same
pattern as Enigmistica's `applications/enigmistica/`). `Paths::root()` resolves, in order: `--root`,
`%POCKETCHESS_ROOT%`, then `<exeDir>/pocketchess` on **both** platforms, so there is no hard-coded
device path.

Staging sources:
- Windows: a post-build step of `PocketChess.vcxproj` copies from `data/` + `out/engines/*/windows-x64/`,
  and from `data/generated/` when present (otherwise `data/samples/` packs).
- PB: `build-zig.ps1` copies the same inputs using a shared list, `projects/bundle-manifest.txt`,
  read by both scripts, so the two bundles cannot diverge.

## 6. Parity rules (D1)

1. All features are written in `src/core`, `src/ui` or `src/app`. A platform file may only translate
   events, present pixels, spawn processes, or report power/lifecycle state.
2. Every new `.cpp` is added to **both** its `.vcxproj` and `projects/pocketbook/sources.cmake`.
   `check-sources.ps1` runs as a pre-build step of `PchessCore`, so the build fails on drift.
3. `scripts/verify-all.ps1` runs MSVC Debug + Release builds, the tests, the PB Release build, the ELF
   checks, and the source/include-boundary checks. Run it before closing any milestone.
4. Anything that behaves differently on the device (refresh modes, key codes, timings) is written down in
   `docs/project-status.md` → "Verified on hardware" with the date and firmware.
