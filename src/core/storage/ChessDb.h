// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// user/chess.db (architecture doc §9): settings, played games, favorites. Never shipped or
// overwritten by deploy/staging (AGENTS.md "Data").

#include "core/storage/Database.h"

#include <map>
#include <span>
#include <string>

namespace pchess::storage {

std::span<const Migration> chessDbMigrations();

// Opens (creating if needed) and migrates to the latest schema.
Result<Database> openChessDb(const std::filesystem::path& file);

// settings(key, value): the app serializes its Settings struct into these strings.
Result<std::map<std::string, std::string>> loadSettings(Database& db);
// Upserts every pair in one transaction.
Result<void> saveSettings(Database& db, const std::map<std::string, std::string>& values);

} // namespace pchess::storage
