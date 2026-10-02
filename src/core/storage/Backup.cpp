// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/storage/Backup.h"

namespace pchess::storage {

Result<void> backupDatabase(Database& db, const std::filesystem::path& dest) {
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) return makeError(ErrorCode::Io, pathToUtf8(dest) + " already exists");
    std::string path = pathToUtf8(dest);
    std::string quoted;
    for (char ch : path) quoted += ch == '\'' ? std::string("''") : std::string(1, ch);
    return db.exec("VACUUM INTO '" + quoted + "'");
}

Result<int> checkBackupFile(const std::filesystem::path& file, int latest) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return makeError(ErrorCode::NotFound, pathToUtf8(file.filename()) + " is missing");
    auto db = Database::open(file, Database::Mode::ReadOnly);
    if (!db) return db.error();
    const int v = db->userVersion();
    if (v < 1) return makeError(ErrorCode::Unsupported, pathToUtf8(file.filename()) + " is not a PocketChess database");
    if (v > latest)
        return makeError(ErrorCode::Unsupported, pathToUtf8(file.filename()) + " comes from a newer PocketChess (schema " +
                                                     std::to_string(v) + ", this app knows " + std::to_string(latest) + ")");
    return v;
}

} // namespace pchess::storage
