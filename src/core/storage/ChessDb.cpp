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
};

} // namespace

std::span<const Migration> chessDbMigrations() { return kSteps; }

Result<Database> openChessDb(const std::filesystem::path& file) {
    auto db = Database::open(file, Database::Mode::ReadWrite);
    if (!db) return db.error();
    if (auto r = migrate(*db, chessDbMigrations()); !r) return r.error();
    return db;
}

Result<std::map<std::string, std::string>> loadSettings(Database& db) {
    auto s = db.prepare("SELECT key, value FROM settings");
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

Result<void> saveSettings(Database& db, const std::map<std::string, std::string>& values) {
    return db.transaction([&]() -> Result<void> {
        auto s = db.prepare("INSERT INTO settings(key, value) VALUES(?1, ?2) "
                            "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
        if (!s) return s.error();
        for (const auto& [k, v] : values) {
            s->bind(1, k).bind(2, v);
            if (auto r = s->run(); !r) return r;
        }
        return {};
    });
}

} // namespace pchess::storage
