// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Backups of the user databases (M7, ADR 0017): consistent single-file copies made with SQLite's
// VACUUM INTO (safe while the database is open in WAL mode), and checks before a restore.

#include "core/storage/Database.h"

#include <filesystem>

namespace pchess::storage {

// Writes a compact copy of `db` to `dest` (which must not exist yet).
Result<void> backupDatabase(Database& db, const std::filesystem::path& dest);

// A file that can replace a user database: an SQLite database whose schema version is between 1
// and `latest` (a newer one comes from a newer app). Returns that version.
Result<int> checkBackupFile(const std::filesystem::path& file, int latest);

} // namespace pchess::storage
