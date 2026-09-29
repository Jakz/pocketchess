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

// played_games: finished games (Play mode), newest first when listed.
struct PlayedGame {
    int64_t id = 0;
    int64_t date = 0;  // unix seconds
    std::string pgn;
    std::string result;  // 1-0, 0-1, 1/2-1/2, *
    std::string mode = "play";
    std::string strength;
    std::string engine;
};
Result<int64_t> addPlayedGame(Database& db, const PlayedGame& game);  // returns the new id
Result<std::vector<PlayedGame>> recentPlayedGames(Database& db, int limit);

} // namespace pchess::storage
