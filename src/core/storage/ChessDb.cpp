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
