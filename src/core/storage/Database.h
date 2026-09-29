// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RAII wrapper over SQLite (architecture doc §9, P4). All connections live on the UI thread
// (SQLITE_THREADSAFE=0). Failures are Results: corruption -> CorruptData, the rest -> Io.

#include "core/util/Error.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace pchess::storage {

class Database;

// One prepared statement. Bind indices are 1-based, column indices 0-based (SQLite's convention).
class Statement {
public:
    Statement() = default;
    ~Statement();
    Statement(Statement&& o) noexcept;
    Statement& operator=(Statement&& o) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, int64_t v);
    Statement& bind(int index, int v) { return bind(index, static_cast<int64_t>(v)); }
    Statement& bind(int index, double v);
    Statement& bind(int index, std::string_view v);  // copied (SQLITE_TRANSIENT)
    Statement& bind(int index, const char* v) { return bind(index, std::string_view(v)); }
    Statement& bindBlob(int index, std::span<const uint8_t> v);
    Statement& bindNull(int index);

    // true: a row is available; false: done. The first bind error is reported here too.
    Result<bool> step();
    // Runs a statement that returns no rows (or ignores them) to completion.
    Result<void> run();
    void reset();  // keeps bindings; clears the pending bind error

    int columns() const;
    bool isNull(int col) const;
    int64_t int64(int col) const;
    int integer(int col) const { return static_cast<int>(int64(col)); }
    double real(int col) const;
    std::string text(int col) const;
    std::vector<uint8_t> blob(int col) const;

private:
    friend class Database;
    Statement(sqlite3_stmt* s, Database* db) : stmt_(s), db_(db) {}
    void noteBind(int rc);

    sqlite3_stmt* stmt_ = nullptr;
    Database* db_ = nullptr;
    int bindError_ = 0;
};

class Database {
public:
    enum class Mode {
        ReadWrite,  // user data: created if missing, WAL (DELETE if WAL is unavailable), synchronous=NORMAL
        ReadOnly,   // content packs: read-only and immutable (no locks, no journal)
        Memory,     // tests: private in-memory database (the path is ignored)
    };

    Database() = default;
    ~Database();
    Database(Database&& o) noexcept;
    Database& operator=(Database&& o) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    static Result<Database> open(const std::filesystem::path& file, Mode mode);

    bool isOpen() const { return db_ != nullptr; }
    void close();

    Result<void> exec(std::string_view sql);  // one or more statements, no results
    Result<Statement> prepare(std::string_view sql);

    // BEGIN IMMEDIATE ... COMMIT; `body` failing (or returning an error) rolls back.
    Result<void> transaction(const std::function<Result<void>()>& body);

    int userVersion();
    Result<void> setUserVersion(int v);
    int64_t lastInsertId() const;
    int changes() const;
    std::string journalMode();  // "wal", "delete", "memory" ... (lower case)
    const std::filesystem::path& file() const { return file_; }

    // Error for the connection's last failure, with `what` as context.
    Error error(std::string_view what, int rc) const;

private:
    sqlite3* db_ = nullptr;
    std::filesystem::path file_;
};

// Ordered schema steps: step N brings user_version from N-1 to N (architecture doc §9).
struct Migration {
    int version;
    const char* sql;
};

// Applies every step above the current user_version, each in its own transaction together with
// the version bump. A database newer than the steps is an error (a downgrade would lose data).
Result<void> migrate(Database& db, std::span<const Migration> steps);

// Path as UTF-8 for SQLite (Windows paths are UTF-16 internally).
std::string pathToUtf8(const std::filesystem::path& p);

} // namespace pchess::storage
