# Chess Study for PocketBook — Design Notes

## 1. Vision

A lightweight, fully offline chess application designed primarily for a 6" color e-ink reader such as the PocketBook Verse Pro Color.

The application should combine four main use cases:

1. **Play** against Stockfish.
2. **Analyze** arbitrary positions and completed games.
3. **Study historical games** from a local PGN database.
4. **Train tactics** from a large local puzzle database with an internal rating and spaced review.

The UI should be explicitly optimized for e-ink:
- no unnecessary animation;
- partial screen refreshes wherever possible;
- large touch targets;
- support for both touch and hardware buttons;
- configurable color and monochrome rendering;
- no dependency on an internet connection.

The application should feel like a dedicated chess appliance rather than a desktop chess GUI squeezed onto an e-reader.


---

# 2. Primary goals

## 2.1 Functional goals

The first complete version should provide:

- Stockfish integration through UCI.
- Human vs engine play.
- Position analysis.
- PGN game browsing.
- Historical game database.
- Tactical puzzle trainer.
- Internal player/puzzle rating.
- Review queue for failed puzzles.
- FEN import/export.
- PGN import.
- Local persistence of:
  - settings;
  - game history;
  - puzzle progress;
  - puzzle rating;
  - engine preferences;
  - favorites/bookmarks.
- Custom piece sets.
- Custom board themes.
- Color and monochrome modes.
- Portrait-first UI optimized for 6" e-ink.


## 2.2 Non-goals for the first version

Avoid scope creep initially:

- no online chess;
- no Chess.com/Lichess live login;
- no cloud synchronization;
- no multiplayer networking;
- no animated piece movement;
- no opening database generated dynamically from millions of games on-device;
- no neural network engine;
- no heavy graphical effects.

All external content should be prepared or imported offline.


---

# 3. Target platform

Primary target:

- PocketBook Verse Pro Color
- Linux-based PocketBook firmware
- InkView native application
- ARM CPU
- 6" Kaleido 3 display
- 300 ppi monochrome / 150 ppi effective color
- touch input
- hardware page buttons
- limited RAM compared with modern phones/tablets

Secondary future targets:

- other PocketBook devices;
- desktop Linux;
- Windows;
- Android;
- Kobo, using an alternate frontend.

The chess core should therefore be platform-independent wherever possible.


---

# 4. High-level architecture

```text
+----------------------------------------------------+
|                    Application                     |
+----------------------------------------------------+
| UI / InkView frontend                              |
| - board renderer                                   |
| - touch handling                                   |
| - dialogs / menus                                  |
| - e-ink refresh manager                            |
+----------------------------------------------------+
| Application services                               |
| - GameController                                   |
| - AnalysisController                               |
| - PuzzleTrainer                                    |
| - StudyBrowser                                     |
| - RatingService                                    |
| - ReviewScheduler                                  |
+----------------------------------------------------+
| Chess domain                                       |
| - Board / Move / Position                          |
| - PGN parser                                       |
| - FEN parser                                       |
| - SAN generator                                    |
| - legal move handling                              |
+----------------------------------------------------+
| Data                                               |
| - SQLite                                           |
| - puzzle database                                  |
| - game database                                    |
| - progress database                                |
+----------------------------------------------------+
| Engines                                            |
| - Stockfish process                                |
| - UCI adapter                                      |
+----------------------------------------------------+
```

The UI must not know Stockfish details directly.

Stockfish should be treated as a replaceable external engine behind a small UCI abstraction.


---

# 5. Suggested project structure

```text
src/
  app/
    Application.cpp
    Settings.cpp

  chess/
    Board.cpp
    Move.cpp
    Position.cpp
    Fen.cpp
    Pgn.cpp
    San.cpp

  engine/
    UciEngine.cpp
    StockfishEngine.cpp
    EngineManager.cpp

  puzzles/
    Puzzle.cpp
    PuzzleDatabase.cpp
    PuzzleTrainer.cpp
    RatingService.cpp
    ReviewScheduler.cpp

  games/
    GameDatabase.cpp
    GameBrowser.cpp
    GameStudy.cpp

  ui/
    Screen.cpp
    BoardView.cpp
    PieceRenderer.cpp
    MenuView.cpp
    AnalysisPanel.cpp
    PuzzleView.cpp
    GameView.cpp
    SettingsView.cpp
    ThemeManager.cpp

  eink/
    RefreshManager.cpp
    DirtyRect.cpp

  storage/
    Database.cpp
    Migrations.cpp

assets/
  pieces/
  boards/
  fonts/
  openings/
  icons/

tools/
  puzzle_importer/
  pgn_importer/
  asset_converter/
```

Keep the game/database/import pipeline separate from the embedded reader application.


---

# 6. Main application modes

## 6.1 Home

Minimal home screen:

```text
CHESS

[ Play ]
[ Tactics ]
[ Games ]
[ Analyze ]
[ Review ]

Last session:
Puzzle rating 1482
Review due: 23
```

Optional footer:

```text
Battery 76%        Engine: SF
```

Avoid a permanently animated status area.


---

# 7. Play mode

## 7.1 Features

- Human vs Stockfish.
- Select:
  - white;
  - black;
  - random.
- Difficulty modes:
  - approximate Elo;
  - fixed depth;
  - fixed move time.
- Optional opening book.
- Undo.
- Resign.
- Flip board.
- Save completed game as PGN.
- Analyze after game.


## 7.2 Engine strength

User-facing difficulty should be simple:

```text
Beginner       ~800
Casual        ~1200
Club          ~1600
Strong        ~2000
Maximum
```

Internally, use Stockfish UCI options where available:

- `UCI_LimitStrength`
- `UCI_Elo`

For Maximum mode:

- disable strength limiting;
- allow configurable depth / move time.


## 7.3 E-ink behavior

After a move:

1. erase/redraw source square;
2. redraw destination square;
3. update captured piece square if necessary;
4. update move text;
5. perform a partial refresh only on affected rectangles.

Periodically perform a full board refresh to clear ghosting.


---

# 8. Analysis mode

Analysis should work from:

- current game;
- historical game position;
- puzzle;
- imported FEN;
- imported PGN.

## 8.1 Basic view

```text
+------------------+
|     chessboard   |
|                  |
+------------------+

+0.62
1. Nf3   Nf6
2. c4    e6

PV:
Nf3 Nf6 c4 e6
```

Default analysis should show:

- evaluation;
- best move;
- principal variation;
- optional depth;
- optional nodes/time in advanced mode.


## 8.2 Engine activation policy

Stockfish should **not run continuously unless needed**.

Possible states:

```text
Engine OFF
Engine QUICK
Engine DEEP
```

Suggested defaults:

- OFF during normal reading/browsing;
- QUICK for hints;
- DEEP only when explicitly requested.

This matters significantly for battery life.


## 8.3 Analysis controls

- Start/stop engine.
- MultiPV:
  - 1 by default;
  - optionally 2 or 3.
- Depth limit.
- Time limit.
- Flip board.
- Try move.
- Return to main line.
- Save annotation.


---

# 9. Tactical puzzle trainer

This should be a first-class mode, not an accessory.

## 9.1 Puzzle representation

A puzzle record should contain at minimum:

```text
id
fen
moves
rating
rating_deviation (optional)
themes
opening_tags
popularity
source
```

Example conceptual record:

```yaml
id: abc123
fen: "..."
moves:
  - e2e4
  - e7e5
  - g1f3
rating: 1472
themes:
  - fork
  - middlegame
source: lichess
```


## 9.2 Suggested database size

Ship or generate configurable packs:

```text
Starter       5,000 puzzles
Standard     25,000 puzzles
Large        50,000 puzzles
Huge        100,000 puzzles
```

50,000 is a good default target.

On-device storage should remain modest if the database is compact.


## 9.3 Puzzle selection

Select puzzles according to:

- user rating;
- theme;
- recent failures;
- unseen status;
- difficulty range;
- manual filters.

Default adaptive range:

```text
user rating ±150
```


## 9.4 Puzzle interaction

Typical loop:

```text
position shown
    ↓
user move
    ↓
correct?
  /     \
yes     no
 |       |
opponent  allow retry
reply     mark failure
 |
next user move
```

Do not start Stockfish for every move.

Use the stored solution line first.

Stockfish should be used only for:

- validating an alternate correct move;
- explaining why a move is inferior;
- optional post-puzzle analysis.


---

# 10. Internal rating

The app should maintain a local tactical rating.

Two reasonable options:

## Option A — Elo-like

Simple, understandable, easy to implement.

```text
expected = 1 / (1 + 10^((puzzleRating-userRating)/400))
newRating = oldRating + K * (score - expected)
```

Advantages:

- very easy;
- transparent;
- sufficient for personal training.

## Option B — Glicko-2

More sophisticated:

- rating;
- rating deviation;
- volatility.

Useful if training is irregular and rating confidence matters.

Recommendation:

**Start with Elo-like rating.**

Possible K:

```text
K = 24
```

Scoring could consider:

```text
1.0  solved first try
0.7  solved after hesitation / minor mistake
0.4  solved with hint
0.0  failed
```

However, for clean rating behavior, binary solved/failed is preferable initially.


---

# 11. Review / spaced repetition

Failed puzzles should not simply disappear.

Maintain a review queue.

Example schedule:

```text
first failure      -> review in 1 day
second success     -> 3 days
third success      -> 7 days
fourth success     -> 21 days
later              -> 60 days
```

A puzzle should return sooner after another failure.

Track:

```text
attempt_count
success_count
failure_count
last_attempt
next_review
current_interval
```

Home screen can show:

```text
Review due: 23
```


---

# 12. Puzzle themes

Useful filters:

```text
fork
pin
skewer
sacrifice
discoveredAttack
doubleCheck
deflection
decoy
clearance
interference
overloading
promotion
mateIn1
mateIn2
mateIn3
endgame
opening
middlegame
defensiveMove
quietMove
```

Allow:

- include theme;
- exclude theme;
- random;
- rating-limited session.


---

# 13. Historical games database

## 13.1 Input format

Use PGN as the canonical source format.

At import time, normalize into an indexed SQLite database.


## 13.2 Metadata

Store:

```text
event
site
date
round
white
black
white_elo
black_elo
result
eco
opening
variation
pgn_text or move blob
annotations
source
```


## 13.3 Browsing

Allow filters by:

- player;
- year;
- event;
- opening;
- ECO;
- result;
- favorites.

Example:

```text
Games
  Players
    Fischer
    Kasparov
    Karpov
    Carlsen
  World Championships
  Famous Games
  Openings
```


## 13.4 Study navigation

Hardware buttons are ideal:

```text
left/page-up    previous move
right/page-down next move
```

Touch:

- tap move in move list;
- swipe optional;
- tap board square to inspect.


## 13.5 Annotations

Support standard PGN comments and variations.

On a small screen, show comments below the board rather than beside it.

Possible page:

```text
[board]

24. Rxd4!

Kasparov sacrifices the exchange...

[Prev] [Main] [Next]
```


---

# 14. Famous games packs

Optional downloadable/importable packs:

```text
World Champions
Fischer
Tal
Kasparov
Karpov
Carlsen
Capablanca
Morphy
Botvinnik
Alekhine
Endgame Classics
Attacking Classics
Positional Classics
```

Do not hard-wire licensing assumptions into the application.

The importer should work with arbitrary PGN files supplied by the user.


---

# 15. Opening study

Not mandatory for v1, but architecture should support it.

Possible sources:

- curated PGN repertoire;
- opening tree generated offline;
- ECO metadata.

UI:

```text
Ruy Lopez
1. e4 e5
2. Nf3 Nc6
3. Bb5

Main:
3...a6        58%
3...Nf6       31%
3...Bc5        6%
```

For a purely offline personal app, frequency data can be precomputed on desktop.


---

# 16. Endgame study

Future mode:

- opposition;
- king+pawn;
- Lucena;
- Philidor;
- rook endings;
- basic mates.

Store:

```text
FEN
title
description
goal
solution
tags
```

Example:

```text
Lucena Position

Goal:
Win as White.

[ Start ]
```


---

# 17. Board rendering

## 17.1 Requirements

Support:

- color;
- monochrome;
- high-contrast monochrome;
- inverted board;
- coordinates;
- last-move highlight;
- legal-move markers;
- selected-square marker;
- check indicator.

Everything should remain readable at 150 ppi color and 300 ppi B/W.


## 17.2 Color mode

Example palette concepts:

```text
light square: warm cream
dark square: muted green / brown / blue
selected: strong accent
last move: subtle yellow
check: muted red
```

Avoid colors that depend on high saturation because Kaleido 3 is muted.


## 17.3 Monochrome mode

Monochrome should be a first-class setting, not an accessibility fallback.

Possible style:

```text
light square: white
dark square: 25–40% gray
selection: hatched border
last move: dotted border
check: double border
```

Do not rely solely on color to convey game state.


---

# 18. Piece sets

Support externally loadable piece sets.

Suggested asset layout:

```text
assets/pieces/merida/
  white_king.png
  white_queen.png
  ...
  black_pawn.png

assets/pieces/alpha/
...

assets/pieces/custom-name/
...
```

Also support vector-like source assets converted at build/import time.

Recommended built-in sets:

- Merida-like classic;
- Alpha-like;
- minimalist;
- high-contrast e-ink;
- pixel/retro optional.


## 18.1 Asset rules

Piece assets should support:

- transparent background;
- multiple resolutions;
- monochrome assets;
- optional color assets.

Do not rescale expensive source assets on every redraw.

Precompute display-sized bitmaps.


---

# 19. Board themes

Theme descriptor:

```yaml
name: Paper
mode: color
light: "#e8dfc9"
dark: "#8f9b72"
selected: "#c8b95b"
last_move: "#d2c47a"
check: "#b56a61"
coordinates: true
```

For monochrome:

```yaml
name: ClassicMono
mode: mono
light_gray: 255
dark_gray: 150
selected_pattern: outline
last_move_pattern: dots
```


---

# 20. Settings

Suggested settings tree:

```text
Settings

Display
  Board theme
  Piece set
  Color / Monochrome
  Coordinates
  Orientation
  Full refresh interval

Engine
  Strength
  Threads
  Hash
  Analysis depth
  MultiPV

Tactics
  Rating range
  Themes
  Review enabled
  Auto-next puzzle
  Show opponent move automatically

Games
  Default database
  Show annotations
  Show evaluation

Input
  Hardware buttons
  Tap-to-move
  Drag-to-move

Power
  Disable engine when idle
  Analysis timeout

Advanced
  Database path
  Engine binary
  Debug logging
```


---

# 21. Input model

## 21.1 Touch

Primary board interaction:

### Tap-to-move

```text
tap source square
tap destination square
```

Preferred for e-ink.

Optional:

### Drag-to-move

Useful, but not necessary.

Dragging can generate unnecessary screen updates; therefore it should not redraw the piece continuously.

Instead:

```text
touch down -> remember source
touch up   -> determine destination
```


## 21.2 Hardware buttons

Default mapping:

```text
Game browser:
  left button  previous move
  right button next move

Menus:
  left button  up
  right button down

Puzzle:
  buttons      previous/next puzzle after completion
```

Allow remapping.


---

# 22. E-ink refresh strategy

A dedicated RefreshManager is important.

## 22.1 Dirty rectangles

Every view should report changed regions:

```cpp
refresh.markDirty(board.square(e2));
refresh.markDirty(board.square(e4));
refresh.markDirty(statusRect);
refresh.flushPartial();
```

Avoid full-screen refreshes after every move.


## 22.2 Full refresh policy

Possible default:

```text
full refresh every 12 moves
```

or when:

- changing screen;
- changing theme;
- significant ghosting;
- opening a dialog covering most of the display.

Make the interval user-configurable.


## 22.3 Color vs monochrome refresh

Allow the renderer to choose a refresh strategy based on:

```text
Color
Monochrome
Fast monochrome
```

If device APIs expose distinct update modes, wrap them behind the platform abstraction.


---

# 23. Stockfish integration

## 23.1 Process model

Run Stockfish as a child process.

Communication:

```text
stdin  -> UCI commands
stdout -> parsed engine responses
```

Basic initialization:

```text
uci
setoption name Threads value 1
setoption name Hash value 32
isready
```


## 23.2 Recommended defaults for PocketBook

Conservative:

```text
Threads: 1
Hash: 16–32 MB
MultiPV: 1
```

The goal is responsiveness with low power use, not maximum engine strength.


## 23.3 Engine tasks

Provide asynchronous jobs:

```text
BestMoveJob
AnalysisJob
HintJob
ValidateAlternativeJob
```

UI must never block waiting for Stockfish.


## 23.4 Engine shutdown

Stop analysis immediately when:

- application goes to background;
- device sleeps;
- user exits analysis;
- low-battery policy triggers.

Send:

```text
stop
quit
```

where appropriate.


---

# 24. Databases

Use SQLite.

Suggested databases:

```text
chess.db
  user_settings
  played_games
  favorites

puzzles.db
  puzzles
  puzzle_themes

progress.db
  puzzle_progress
  rating_history
  review_queue

games.db
  games
  players
  events
  openings
```


## 24.1 Why multiple databases

Benefits:

- puzzle/game packs can be replaced independently;
- user state is never overwritten;
- easier distribution;
- easier desktop preprocessing.


---

# 25. Puzzle database schema

Example:

```sql
CREATE TABLE puzzles (
    id TEXT PRIMARY KEY,
    fen TEXT NOT NULL,
    solution BLOB NOT NULL,
    rating INTEGER,
    popularity INTEGER,
    source TEXT
);

CREATE TABLE puzzle_themes (
    puzzle_id TEXT NOT NULL,
    theme TEXT NOT NULL
);

CREATE INDEX idx_puzzles_rating
ON puzzles(rating);
```


Progress:

```sql
CREATE TABLE puzzle_progress (
    puzzle_id TEXT PRIMARY KEY,
    attempts INTEGER NOT NULL DEFAULT 0,
    successes INTEGER NOT NULL DEFAULT 0,
    failures INTEGER NOT NULL DEFAULT 0,
    last_result INTEGER,
    last_attempt INTEGER,
    next_review INTEGER,
    interval_days INTEGER
);
```


---

# 26. Historical game schema

```sql
CREATE TABLE games (
    id INTEGER PRIMARY KEY,
    event TEXT,
    site TEXT,
    date TEXT,
    round TEXT,
    white TEXT,
    black TEXT,
    white_elo INTEGER,
    black_elo INTEGER,
    result TEXT,
    eco TEXT,
    opening TEXT,
    moves BLOB,
    pgn TEXT
);

CREATE INDEX idx_games_white ON games(white);
CREATE INDEX idx_games_black ON games(black);
CREATE INDEX idx_games_date ON games(date);
CREATE INDEX idx_games_eco ON games(eco);
```


---

# 27. Desktop preprocessing tools

Heavy preparation should happen on desktop.

## 27.1 Puzzle importer

Input:

```text
Lichess puzzle CSV / Zstandard
```

Output:

```text
puzzles.db
```

Tasks:

- select desired rating range;
- select themes;
- cap total size;
- normalize moves;
- generate compact binary move encoding;
- build indexes.


## 27.2 PGN importer

Input:

```text
*.pgn
```

Output:

```text
games.db
```

Tasks:

- validate PGN;
- normalize player names;
- parse headers;
- generate ECO/opening metadata if available;
- optionally precompute diagrams or critical positions.


## 27.3 Personal game importer

Future tool:

```text
Chess.com/Lichess exported PGN
      ↓
Stockfish desktop analysis
      ↓
detect blunders
      ↓
generate personal puzzles
      ↓
personal_puzzles.db
```

This is particularly valuable because heavy engine analysis remains on the PC.


---

# 28. Personal mistakes mode

Long-term feature:

```text
My Mistakes
```

Import personal PGNs, analyze offline on desktop, and generate puzzles from:

- blunders;
- missed tactics;
- missed defensive moves;
- opening mistakes.

Store metadata:

```text
source_game
move_number
original_move
best_move
evaluation_loss
date
opponent
```

The PocketBook then becomes a personalized tactical trainer.


---

# 29. Search

Historical game search should support:

```text
player
opponent
year
event
ECO
opening
result
```

Do not attempt a complex full-text engine initially.

SQLite indexes are enough.


---

# 30. Save states and session persistence

The application should survive sleep or accidental exit.

Persist:

```text
current screen
current game
current move index
current puzzle
board orientation
analysis state (but not running engine)
```

On relaunch:

```text
Resume?
[ Yes ] [ Home ]
```


---

# 31. Battery-conscious behavior

Rules:

1. Stockfish is off unless explicitly needed.
2. No background indexing on the reader.
3. No periodic animations.
4. Avoid unnecessary full refreshes.
5. No polling loops.
6. Sleep when PocketBook sleeps.
7. Keep database queries bounded and indexed.
8. Precompute assets.
9. Use one engine thread by default.


---

# 32. Error handling

The app should fail gracefully if:

- Stockfish binary missing;
- database missing;
- PGN malformed;
- theme malformed;
- piece set incomplete.

Example:

```text
Stockfish not found.

Expected:
system/chess/stockfish

[ Settings ]
[ Continue without engine ]
```


---

# 33. File layout on device

Suggested:

```text
applications/
  ChessStudy.app

system/
  chess/
    stockfish
    chess.db
    progress.db

    databases/
      puzzles.db
      historical-games.db

    pieces/
      merida/
      minimal/

    themes/
      paper.yaml
      mono.yaml

    imports/
      *.pgn
      *.fen

    logs/
```


---

# 34. Import workflow

The user should be able to copy files by USB.

On startup or from menu:

```text
Import
  PGN
  FEN
  Puzzle database
  Game database
  Piece set
  Board theme
```

Do not scan the entire filesystem continuously.


---

# 35. Custom content format

For manually authored lessons or position packs, YAML is convenient.

Example:

```yaml
title: Lucena Position
type: endgame
fen: "..."
side: white
goal: "Win the position"
solution:
  - ...
notes: |
  Build the bridge using the rook.
tags:
  - rook-endgame
  - lucena
```


---

# 36. Future learning mode

Potential future module:

```text
Learn
  Openings
  Middlegame
  Endgames
  Strategy
```

Lesson structure:

```text
text
diagram
question
interactive position
explanation
next
```

This could eventually become an offline Chessable-like system.


---

# 37. Development milestones

## Milestone 0 — platform prototype

- InkView application launches.
- Draw board.
- Touch selects square.
- Partial refresh works.
- Hardware buttons detected.


## Milestone 1 — chess board

- complete legal move logic;
- FEN loading;
- piece rendering;
- game state;
- promotion;
- castling;
- en passant.


## Milestone 2 — Stockfish

- launch engine;
- UCI communication;
- engine move;
- basic analysis.


## Milestone 3 — Play

- human vs engine;
- save PGN;
- engine strength.


## Milestone 4 — Puzzle trainer

- SQLite puzzle database;
- adaptive selection;
- rating;
- review queue.


## Milestone 5 — Historical games

- PGN importer;
- games database;
- filtering;
- move navigation;
- annotations.


## Milestone 6 — Themes

- custom piece sets;
- color themes;
- monochrome themes;
- refresh tuning.


## Milestone 7 — Study features

- opening packs;
- endgame packs;
- personal mistake import.


---

# 38. MVP definition

A realistic first usable release should contain only:

```text
Play
  Stockfish opponent

Tactics
  25k–50k puzzles
  local rating
  review failed puzzles

Games
  browse PGN database
  next/previous move

Analyze
  Stockfish evaluation

Settings
  piece set
  color/B&W
  engine strength
```

Everything else can wait.


---

# 39. UX principles

1. **One action per screen.**
2. **No animation required to understand state.**
3. **Touch targets large enough for fingers.**
4. **Hardware buttons should always do something useful.**
5. **Never require internet.**
6. **Engine work only when explicitly requested.**
7. **The board should always remain the visual focus.**
8. **The application must remain pleasant in monochrome.**
9. **Color is enhancement, never required information.**
10. **All personal progress belongs in a separate database from content packs.**


---

# 40. Possible application names

Working names:

- ChessPocket
- InkChess
- ChessStudy
- PocketChess
- EInk Chess
- Chess Trek
- PaperChess

`InkChess` or `ChessStudy` would probably fit the project best.


---

# 41. Recommended first technical spike

Before building databases or importers, validate these five things on the real Verse Pro Color:

1. create a minimal InkView app;
2. render a full chessboard;
3. detect touch coordinates reliably;
4. perform partial updates on individual squares;
5. launch a cross-compiled Stockfish binary and exchange basic UCI commands.

If those work reliably, the rest of the project is conventional C++ application development.


---

# 42. Long-term concept

The final application could become a compact offline chess workstation:

```text
              CHESS STUDY
                   |
      +------------+------------+
      |            |            |
     PLAY        TRAIN         STUDY
      |            |            |
 Stockfish      Puzzles       Games
 Analysis       Rating        Openings
 Saved games    Review        Endgames
                Mistakes      Lessons
```

The key idea is to keep the heavy content preparation on a PC and make the PocketBook application a fast, low-power, purpose-built frontend for playing and studying chess.

That division of labor fits the hardware extremely well.
