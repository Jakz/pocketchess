// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/storage/ChessDb.h"

namespace pchess::storage {

namespace {

// Append-only: never edit a released step; add a new one (architecture doc §9).
constexpr Migration kSteps[] = {
    {1, R"sql(
CREATE TABLE settings(
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE played_games(
    id       INTEGER PRIMARY KEY,
    date     INTEGER NOT NULL,        -- unix seconds (util::Clock)
    pgn      TEXT NOT NULL,
    result   TEXT NOT NULL,           -- 1-0, 0-1, 1/2-1/2, *
    mode     TEXT NOT NULL,           -- play, ...
    strength TEXT,                    -- strength label (beginner ... maximum)
    engine   TEXT                     -- engine id
);
CREATE INDEX idx_played_games_date ON played_games(date);

CREATE TABLE favorites(
    pack    TEXT NOT NULL,            -- games pack file name
    game_id INTEGER NOT NULL,
    added   INTEGER NOT NULL,
    PRIMARY KEY(pack, game_id)
) WITHOUT ROWID;
)sql"},
    {2, R"sql(
-- SessionState (design notes §30, ADR 0011): the resumable screen, replaced on every change.
CREATE TABLE session(
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;
)sql"},
    {3, R"sql(
-- The user's side in each played game (Home statistics).
ALTER TABLE played_games ADD COLUMN human TEXT;   -- white / black
)sql"},
    {4, R"sql(
-- Games in progress (several at once): saved after every move, removed when finished or abandoned.
CREATE TABLE active_games(
    id          INTEGER PRIMARY KEY,
    started     INTEGER NOT NULL,     -- unix seconds
    last_played INTEGER NOT NULL,
    human       TEXT NOT NULL,        -- white / black
    strength    TEXT NOT NULL,
    engine      TEXT,                 -- engine name when last played (display)
    fen         TEXT NOT NULL,        -- start position
    moves       TEXT NOT NULL,        -- UCI, space separated
    flipped     INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_active_games_last ON active_games(last_played);
)sql"},
    {5, R"sql(
-- Guess the move (M7): one row per run through a game, updated after every guessed move.
CREATE TABLE guess_runs(
    id         INTEGER PRIMARY KEY,
    pack       TEXT NOT NULL,         -- games pack name ("My games" included)
    game_id    INTEGER NOT NULL,
    side       TEXT NOT NULL,         -- white / black / both
    title      TEXT NOT NULL,         -- "White – Black, 1858" (lists, stats)
    started    INTEGER NOT NULL,      -- unix seconds
    updated    INTEGER NOT NULL,
    from_ply   INTEGER NOT NULL,      -- where the run began
    ply        INTEGER NOT NULL,      -- where it is now
    plies      INTEGER NOT NULL,      -- main-line length
    first      INTEGER NOT NULL DEFAULT 0,
    second     INTEGER NOT NULL DEFAULT 0,
    missed     INTEGER NOT NULL DEFAULT 0,
    finished   INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_guess_runs_updated ON guess_runs(updated);
CREATE INDEX idx_guess_runs_game ON guess_runs(pack, game_id);

-- Every guessed move: outcome 0 = first try, 1 = second try or hint, 2 = missed (shown).
CREATE TABLE guess_moves(
    run_id  INTEGER NOT NULL REFERENCES guess_runs(id) ON DELETE CASCADE,
    ply     INTEGER NOT NULL,         -- the game move's ply (0 = White's first move)
    outcome INTEGER NOT NULL,
    wrong   TEXT,                     -- the first wrong try (UCI), if any
    hinted  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(run_id, ply)
) WITHOUT ROWID;
)sql"},
    {6, R"sql(
-- My mistakes (M7): the user's bad moves in finished Play games, found by the engine, as puzzles.
-- Solving them is recorded in progress.db (pack "My mistakes", puzzle id "m<id>").
CREATE TABLE mistakes(
    id       INTEGER PRIMARY KEY,
    game_id  INTEGER NOT NULL,        -- played_games.id
    ply      INTEGER NOT NULL,        -- the user's move
    fen      TEXT NOT NULL,           -- before the opponent's previous move
    moves    TEXT NOT NULL,           -- UCI: that move, then the better move
    played   TEXT NOT NULL,           -- the user's move (UCI)
    before   INTEGER NOT NULL,        -- the user's winning chance (%) before the move
    after    INTEGER NOT NULL,        -- and after it
    created  INTEGER NOT NULL,
    UNIQUE(game_id, ply)
);

-- Games already checked (found = mistakes stored for it), so each game is analyzed once.
CREATE TABLE mistake_scans(
    game_id INTEGER PRIMARY KEY,
    scanned INTEGER NOT NULL,
    engine  TEXT,
    found   INTEGER NOT NULL
);
)sql"},
    {7, R"sql(
-- Opening practice (M7): one row per drill, updated after every move of the user.
CREATE TABLE drill_runs(
    id       INTEGER PRIMARY KEY,
    pack     TEXT NOT NULL,           -- the game pack whose statistics were the book
    side     TEXT NOT NULL,           -- white / black
    start    TEXT NOT NULL,           -- UCI moves from the standard start before the drill
    opening  TEXT NOT NULL,           -- "B90 Sicilian Defense: Najdorf Variation" or ""
    started  INTEGER NOT NULL,
    updated  INTEGER NOT NULL,
    moves    INTEGER NOT NULL DEFAULT 0,   -- moves played in the drill (both sides)
    main     INTEGER NOT NULL DEFAULT 0,   -- the user's moves: the most played one
    book     INTEGER NOT NULL DEFAULT 0,   -- another book move
    missed   INTEGER NOT NULL DEFAULT 0,   -- not found (the book move was shown)
    finished INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_drill_runs_updated ON drill_runs(updated);

-- Every move of the user: position (FEN), outcome 0 main / 1 book / 2 missed, the move played and its share.
CREATE TABLE drill_moves(
    run_id  INTEGER NOT NULL REFERENCES drill_runs(id) ON DELETE CASCADE,
    ply     INTEGER NOT NULL,         -- from the drill's start
    fen     TEXT NOT NULL,
    played  TEXT NOT NULL,            -- the move made (UCI): the user's, or the shown one
    outcome INTEGER NOT NULL,
    share   INTEGER NOT NULL,         -- % of the book's games with that move
    wrong   TEXT,                     -- the first try outside the book (UCI)
    hinted  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(run_id, ply)
) WITHOUT ROWID;
)sql"},
};

} // namespace

std::span<const Migration> chessDbMigrations() { return kSteps; }

Result<Database> openChessDb(const std::filesystem::path& file) {
    auto db = Database::open(file, Database::Mode::ReadWrite);
    if (!db) return db.error();
    if (auto r = migrate(*db, chessDbMigrations()); !r) return r.error();
    return db;
}

namespace {

// key/value tables (settings, session): the table name is one of ours, never user input.
Result<std::map<std::string, std::string>> loadKeyValues(Database& db, const char* table) {
    auto s = db.prepare(std::string("SELECT key, value FROM ") + table);
    if (!s) return s.error();
    std::map<std::string, std::string> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out[s->text(0)] = s->text(1);
    }
    return out;
}

Result<void> upsertKeyValues(Database& db, const char* table, const std::map<std::string, std::string>& values) {
    auto s = db.prepare(std::string("INSERT INTO ") + table +
                        "(key, value) VALUES(?1, ?2) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    if (!s) return s.error();
    for (const auto& [k, v] : values) {
        s->bind(1, k).bind(2, v);
        if (auto r = s->run(); !r) return r;
    }
    return {};
}

} // namespace

Result<std::map<std::string, std::string>> loadSettings(Database& db) { return loadKeyValues(db, "settings"); }

Result<void> saveSettings(Database& db, const std::map<std::string, std::string>& values) {
    return db.transaction([&] { return upsertKeyValues(db, "settings", values); });
}

Result<std::map<std::string, std::string>> loadSession(Database& db) { return loadKeyValues(db, "session"); }

Result<void> saveSession(Database& db, const std::map<std::string, std::string>& values) {
    return db.transaction([&]() -> Result<void> {
        if (auto r = db.exec("DELETE FROM session"); !r) return r;
        return upsertKeyValues(db, "session", values);
    });
}

Result<void> clearSession(Database& db) { return db.exec("DELETE FROM session"); }

Result<int64_t> addPlayedGame(Database& db, const PlayedGame& g) {
    auto s = db.prepare("INSERT INTO played_games(date, pgn, result, mode, strength, engine, human) "
                        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    if (!s) return s.error();
    s->bind(1, g.date).bind(2, g.pgn).bind(3, g.result).bind(4, g.mode);
    auto textOrNull = [&](int i, const std::string& v) {
        if (v.empty()) s->bindNull(i);
        else s->bind(i, v);
    };
    textOrNull(5, g.strength);
    textOrNull(6, g.engine);
    textOrNull(7, g.human);
    if (auto r = s->run(); !r) return r.error();
    return db.lastInsertId();
}

Result<std::vector<PlayedGame>> recentPlayedGames(Database& db, int limit) {
    auto s = db.prepare("SELECT id, date, pgn, result, mode, strength, engine, human FROM played_games "
                        "ORDER BY date DESC, id DESC LIMIT ?1");
    if (!s) return s.error();
    s->bind(1, limit);
    std::vector<PlayedGame> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(
            {s->int64(0), s->int64(1), s->text(2), s->text(3), s->text(4), s->text(5), s->text(6), s->text(7)});
    }
    return out;
}

Result<void> setFavorite(Database& db, const std::string& pack, int64_t gameId, bool on, int64_t now) {
    auto s = db.prepare(on ? "INSERT OR REPLACE INTO favorites(pack, game_id, added) VALUES(?1, ?2, ?3)"
                           : "DELETE FROM favorites WHERE pack = ?1 AND game_id = ?2");
    if (!s) return s.error();
    s->bind(1, pack).bind(2, gameId);
    if (on) s->bind(3, now);
    return s->run();
}

Result<bool> isFavorite(Database& db, const std::string& pack, int64_t gameId) {
    auto s = db.prepare("SELECT 1 FROM favorites WHERE pack = ?1 AND game_id = ?2");
    if (!s) return s.error();
    s->bind(1, pack).bind(2, gameId);
    auto row = s->step();
    if (!row) return row.error();
    return *row;
}

Result<std::vector<int64_t>> favoriteIds(Database& db, const std::string& pack) {
    auto s = db.prepare("SELECT game_id FROM favorites WHERE pack = ?1 ORDER BY added DESC");
    if (!s) return s.error();
    s->bind(1, pack);
    std::vector<int64_t> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(s->int64(0));
    }
    return out;
}

Result<PlayStats> playStats(Database& db) {
    auto s = db.prepare(R"sql(
SELECT count(*),
       coalesce(sum((human = 'white' AND result = '1-0') OR (human = 'black' AND result = '0-1')), 0),
       coalesce(sum(result = '1/2-1/2'), 0),
       coalesce(sum((human = 'white' AND result = '0-1') OR (human = 'black' AND result = '1-0')), 0)
FROM played_games WHERE human IS NOT NULL AND result <> '*'
)sql");
    if (!s) return s.error();
    auto row = s->step();
    if (!row) return row.error();
    PlayStats st;
    st.games = s->integer(0);
    st.won = s->integer(1);
    st.drawn = s->integer(2);
    st.lost = s->integer(3);
    return st;
}

Result<int64_t> saveActiveGame(Database& db, const ActiveGame& g) {
    auto s = db.prepare(g.id ? "UPDATE active_games SET started = ?1, last_played = ?2, human = ?3, strength = ?4, "
                               "engine = ?5, fen = ?6, moves = ?7, flipped = ?8 WHERE id = ?9"
                             : "INSERT INTO active_games(started, last_played, human, strength, engine, fen, moves, "
                               "flipped) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    if (!s) return s.error();
    s->bind(1, g.started).bind(2, g.lastPlayed).bind(3, g.human).bind(4, g.strength);
    if (g.engine.empty()) s->bindNull(5);
    else s->bind(5, g.engine);
    s->bind(6, g.fen).bind(7, g.moves).bind(8, g.flipped ? 1 : 0);
    if (g.id) s->bind(9, g.id);
    if (auto r = s->run(); !r) return r.error();
    return g.id ? g.id : db.lastInsertId();
}

Result<void> deleteActiveGame(Database& db, int64_t id) {
    auto s = db.prepare("DELETE FROM active_games WHERE id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    return s->run();
}

namespace {
constexpr const char* kActiveColumns =
    "SELECT id, started, last_played, human, strength, engine, fen, moves, flipped FROM active_games";
ActiveGame readActive(const Statement& s) {
    ActiveGame g;
    g.id = s.int64(0);
    g.started = s.int64(1);
    g.lastPlayed = s.int64(2);
    g.human = s.text(3);
    g.strength = s.text(4);
    g.engine = s.text(5);
    g.fen = s.text(6);
    g.moves = s.text(7);
    g.flipped = s.integer(8) != 0;
    return g;
}
} // namespace

Result<std::vector<ActiveGame>> activeGames(Database& db) {
    auto s = db.prepare(std::string(kActiveColumns) + " ORDER BY last_played DESC, id DESC");
    if (!s) return s.error();
    std::vector<ActiveGame> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(readActive(*s));
    }
    return out;
}

Result<std::optional<ActiveGame>> activeGame(Database& db, int64_t id) {
    auto s = db.prepare(std::string(kActiveColumns) + " WHERE id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    auto row = s->step();
    if (!row) return row.error();
    if (!*row) return std::optional<ActiveGame>();
    return std::optional<ActiveGame>(readActive(*s));
}

Result<int> playedGameCount(Database& db) {
    auto s = db.prepare("SELECT count(*) FROM played_games");
    if (!s) return s.error();
    auto row = s->step();
    if (!row) return row.error();
    return s->integer(0);
}

Result<int64_t> saveGuessRun(Database& db, const GuessRun& r) {
    auto s = db.prepare(r.id ? "UPDATE guess_runs SET pack = ?1, game_id = ?2, side = ?3, title = ?4, started = ?5, "
                               "updated = ?6, from_ply = ?7, ply = ?8, plies = ?9, first = ?10, second = ?11, "
                               "missed = ?12, finished = ?13 WHERE id = ?14"
                             : "INSERT INTO guess_runs(pack, game_id, side, title, started, updated, from_ply, ply, "
                               "plies, first, second, missed, finished) "
                               "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)");
    if (!s) return s.error();
    s->bind(1, r.pack).bind(2, r.gameId).bind(3, r.side).bind(4, r.title).bind(5, r.started).bind(6, r.updated);
    s->bind(7, r.fromPly).bind(8, r.ply).bind(9, r.plies).bind(10, r.first).bind(11, r.second).bind(12, r.missed);
    s->bind(13, r.finished ? 1 : 0);
    if (r.id) s->bind(14, r.id);
    if (auto res = s->run(); !res) return res.error();
    return r.id ? r.id : db.lastInsertId();
}

Result<void> addGuessMove(Database& db, int64_t runId, int ply, int outcome, const std::string& wrong, bool hinted) {
    auto s = db.prepare("INSERT OR REPLACE INTO guess_moves(run_id, ply, outcome, wrong, hinted) "
                        "VALUES(?1, ?2, ?3, ?4, ?5)");
    if (!s) return s.error();
    s->bind(1, runId).bind(2, ply).bind(3, outcome);
    if (wrong.empty()) s->bindNull(4);
    else s->bind(4, wrong);
    s->bind(5, hinted ? 1 : 0);
    return s->run();
}

Result<std::optional<GuessRun>> guessRun(Database& db, int64_t id) {
    auto s = db.prepare("SELECT id, pack, game_id, side, title, started, updated, from_ply, ply, plies, first, second, "
                        "missed, finished FROM guess_runs WHERE id = ?1");
    if (!s) return s.error();
    s->bind(1, id);
    auto row = s->step();
    if (!row) return row.error();
    if (!*row) return std::optional<GuessRun>();
    GuessRun r;
    r.id = s->int64(0);
    r.pack = s->text(1);
    r.gameId = s->int64(2);
    r.side = s->text(3);
    r.title = s->text(4);
    r.started = s->int64(5);
    r.updated = s->int64(6);
    r.fromPly = s->integer(7);
    r.ply = s->integer(8);
    r.plies = s->integer(9);
    r.first = s->integer(10);
    r.second = s->integer(11);
    r.missed = s->integer(12);
    r.finished = s->integer(13) != 0;
    return std::optional<GuessRun>(std::move(r));
}

Result<GuessStats> guessStats(Database& db) {
    auto s = db.prepare("SELECT count(*), coalesce(sum(finished), 0), coalesce(sum(first), 0), "
                        "coalesce(sum(second), 0), coalesce(sum(missed), 0) FROM guess_runs");
    if (!s) return s.error();
    auto row = s->step();
    if (!row) return row.error();
    GuessStats st;
    st.runs = s->integer(0);
    st.finished = s->integer(1);
    st.first = s->integer(2);
    st.second = s->integer(3);
    st.missed = s->integer(4);
    return st;
}

Result<std::vector<PlayedGame>> uncheckedPlayedGames(Database& db) {
    auto s = db.prepare("SELECT id, date, pgn, result, mode, strength, engine, human FROM played_games "
                        "WHERE human IS NOT NULL AND result <> '*' "
                        "AND id NOT IN (SELECT game_id FROM mistake_scans) ORDER BY date, id");
    if (!s) return s.error();
    std::vector<PlayedGame> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(
            {s->int64(0), s->int64(1), s->text(2), s->text(3), s->text(4), s->text(5), s->text(6), s->text(7)});
    }
    return out;
}

Result<void> saveMistakeScan(Database& db, int64_t gameId, const std::vector<StoredMistake>& found,
                             const std::string& engine, int64_t now) {
    return db.transaction([&]() -> Result<void> {
        auto ins = db.prepare("INSERT OR IGNORE INTO mistakes(game_id, ply, fen, moves, played, before, after, created) "
                              "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
        if (!ins) return ins.error();
        for (const StoredMistake& m : found) {
            ins->bind(1, gameId).bind(2, m.ply).bind(3, m.fen).bind(4, m.moves).bind(5, m.played);
            ins->bind(6, m.before).bind(7, m.after).bind(8, now);
            if (auto r = ins->run(); !r) return r;
        }
        auto scan = db.prepare("INSERT OR REPLACE INTO mistake_scans(game_id, scanned, engine, found) VALUES(?1, ?2, ?3, ?4)");
        if (!scan) return scan.error();
        scan->bind(1, gameId).bind(2, now).bind(3, engine).bind(4, static_cast<int>(found.size()));
        return scan->run();
    });
}

Result<std::vector<StoredMistake>> mistakes(Database& db) {
    auto s = db.prepare("SELECT id, game_id, ply, fen, moves, played, before, after, created FROM mistakes "
                        "ORDER BY created, id");
    if (!s) return s.error();
    std::vector<StoredMistake> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        StoredMistake m;
        m.id = s->int64(0);
        m.gameId = s->int64(1);
        m.ply = s->integer(2);
        m.fen = s->text(3);
        m.moves = s->text(4);
        m.played = s->text(5);
        m.before = s->integer(6);
        m.after = s->integer(7);
        m.created = s->int64(8);
        out.push_back(std::move(m));
    }
    return out;
}

Result<int64_t> saveDrillRun(Database& db, const DrillRun& r) {
    auto s = db.prepare(r.id ? "UPDATE drill_runs SET pack = ?1, side = ?2, start = ?3, opening = ?4, started = ?5, "
                               "updated = ?6, moves = ?7, main = ?8, book = ?9, missed = ?10, finished = ?11 WHERE id = ?12"
                             : "INSERT INTO drill_runs(pack, side, start, opening, started, updated, moves, main, book, "
                               "missed, finished) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)");
    if (!s) return s.error();
    s->bind(1, r.pack).bind(2, r.side).bind(3, r.start).bind(4, r.opening).bind(5, r.started).bind(6, r.updated);
    s->bind(7, r.moves).bind(8, r.main).bind(9, r.book).bind(10, r.missed).bind(11, r.finished ? 1 : 0);
    if (r.id) s->bind(12, r.id);
    if (auto res = s->run(); !res) return res.error();
    return r.id ? r.id : db.lastInsertId();
}

Result<void> addDrillMove(Database& db, int64_t runId, const DrillMove& m) {
    auto s = db.prepare("INSERT OR REPLACE INTO drill_moves(run_id, ply, fen, played, outcome, share, wrong, hinted) "
                        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    if (!s) return s.error();
    s->bind(1, runId).bind(2, m.ply).bind(3, m.fen).bind(4, m.played).bind(5, m.outcome).bind(6, m.share);
    if (m.wrong.empty()) s->bindNull(7);
    else s->bind(7, m.wrong);
    s->bind(8, m.hinted ? 1 : 0);
    return s->run();
}

Result<DrillStats> drillStats(Database& db) {
    auto s = db.prepare("SELECT count(*), coalesce(sum(main), 0), coalesce(sum(book), 0), coalesce(sum(missed), 0) "
                        "FROM drill_runs");
    if (!s) return s.error();
    auto row = s->step();
    if (!row) return row.error();
    DrillStats st;
    st.runs = s->integer(0);
    st.main = s->integer(1);
    st.book = s->integer(2);
    st.missed = s->integer(3);
    return st;
}

Result<std::vector<PlayedGame>> allPlayedGames(Database& db) {
    auto s = db.prepare("SELECT id, date, pgn, result, mode, strength, engine, human FROM played_games "
                        "ORDER BY date, id");
    if (!s) return s.error();
    std::vector<PlayedGame> out;
    for (;;) {
        auto row = s->step();
        if (!row) return row.error();
        if (!*row) break;
        out.push_back(
            {s->int64(0), s->int64(1), s->text(2), s->text(3), s->text(4), s->text(5), s->text(6), s->text(7)});
    }
    return out;
}

} // namespace pchess::storage
