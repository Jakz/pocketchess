// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// user/chess.db (architecture doc §9): settings, played games, favorites. Never shipped or
// overwritten by deploy/staging (AGENTS.md "Data").

#include "core/storage/Database.h"

#include <map>
#include <span>
#include <string>
#include <vector>

namespace pchess::storage {

std::span<const Migration> chessDbMigrations();

// Opens (creating if needed) and migrates to the latest schema.
Result<Database> openChessDb(const std::filesystem::path& file);

// settings(key, value): the app serializes its Settings struct into these strings.
Result<std::map<std::string, std::string>> loadSettings(Database& db);
// Upserts every pair in one transaction.
Result<void> saveSettings(Database& db, const std::map<std::string, std::string>& values);

// session: SessionState of the resumable screen (design notes §30, ADR 0011). saveSession
// replaces the whole table in one transaction; an empty table means "nothing to resume".
Result<std::map<std::string, std::string>> loadSession(Database& db);
Result<void> saveSession(Database& db, const std::map<std::string, std::string>& values);
Result<void> clearSession(Database& db);

// played_games: finished games (Play mode), newest first when listed.
struct PlayedGame {
    int64_t id = 0;
    int64_t date = 0;  // unix seconds
    std::string pgn;
    std::string result;  // 1-0, 0-1, 1/2-1/2, *
    std::string mode = "play";
    std::string strength;
    std::string engine;
    std::string human;  // "white" / "black" (the user's side); empty for games from before v3
};
Result<int64_t> addPlayedGame(Database& db, const PlayedGame& game);  // returns the new id
Result<std::vector<PlayedGame>> recentPlayedGames(Database& db, int limit);

// favorites(pack, game_id): games starred in the Games screens (design notes §13.3).
Result<void> setFavorite(Database& db, const std::string& pack, int64_t gameId, bool on, int64_t now);
Result<bool> isFavorite(Database& db, const std::string& pack, int64_t gameId);
Result<std::vector<int64_t>> favoriteIds(Database& db, const std::string& pack);

// Totals from the user's side (games with a known side and a result), for Home.
struct PlayStats {
    int games = 0, won = 0, drawn = 0, lost = 0;
};
Result<PlayStats> playStats(Database& db);

} // namespace pchess::storage
