// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/storage/Database.h"

#include "core/util/Log.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace pchess::storage {

namespace {

// "file:" URI with percent-encoding; Windows drive paths need a leading slash (file:/C:/...).
std::string fileUri(const std::filesystem::path& p) {
    std::string path = pathToUtf8(p);
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string uri = "file:";
    if (path.size() >= 2 && path[1] == ':') uri += '/';
    for (char ch : path) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '.' || c == '_' || c == '~' || c == '/' || c == ':';
        if (plain) {
            uri += ch;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            uri += buf;
        }
    }
    return uri;
}

} // namespace

std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// --- Statement ---------------------------------------------------------------------------------

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& o) noexcept
    : stmt_(std::exchange(o.stmt_, nullptr)), db_(std::exchange(o.db_, nullptr)), bindError_(o.bindError_) {}

Statement& Statement::operator=(Statement&& o) noexcept {
    if (this != &o) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = std::exchange(o.stmt_, nullptr);
        db_ = std::exchange(o.db_, nullptr);
        bindError_ = o.bindError_;
    }
    return *this;
}

void Statement::noteBind(int rc) {
    if (rc != SQLITE_OK && bindError_ == SQLITE_OK) bindError_ = rc;
}

Statement& Statement::bind(int index, int64_t v) {
    noteBind(sqlite3_bind_int64(stmt_, index, v));
    return *this;
}

Statement& Statement::bind(int index, double v) {
    noteBind(sqlite3_bind_double(stmt_, index, v));
    return *this;
}

Statement& Statement::bind(int index, std::string_view v) {
    noteBind(sqlite3_bind_text(stmt_, index, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT));
    return *this;
}

Statement& Statement::bindBlob(int index, std::span<const uint8_t> v) {
    // A zero-length blob still binds as a blob, not NULL.
    noteBind(sqlite3_bind_blob(stmt_, index, v.empty() ? "" : static_cast<const void*>(v.data()),
                               static_cast<int>(v.size()), SQLITE_TRANSIENT));
    return *this;
}

Statement& Statement::bindNull(int index) {
    noteBind(sqlite3_bind_null(stmt_, index));
    return *this;
}

Result<bool> Statement::step() {
    if (bindError_ != SQLITE_OK) return db_->error("bind", bindError_);
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    return db_->error(sqlite3_sql(stmt_), rc);
}

Result<void> Statement::run() {
    for (;;) {
        auto r = step();
        if (!r) return r.error();
        if (!*r) break;
    }
    reset();
    return {};
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    bindError_ = SQLITE_OK;
}

int Statement::columns() const { return sqlite3_column_count(stmt_); }
bool Statement::isNull(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }
int64_t Statement::int64(int col) const { return sqlite3_column_int64(stmt_, col); }
double Statement::real(int col) const { return sqlite3_column_double(stmt_, col); }

std::string Statement::text(int col) const {
    const unsigned char* t = sqlite3_column_text(stmt_, col);
    const int n = sqlite3_column_bytes(stmt_, col);
    return t ? std::string(reinterpret_cast<const char*>(t), static_cast<size_t>(n)) : std::string();
}

std::vector<uint8_t> Statement::blob(int col) const {
    const auto* b = static_cast<const uint8_t*>(sqlite3_column_blob(stmt_, col));
    const int n = sqlite3_column_bytes(stmt_, col);
    return b ? std::vector<uint8_t>(b, b + n) : std::vector<uint8_t>();
}

// --- Database ----------------------------------------------------------------------------------

Database::~Database() { close(); }

Database::Database(Database&& o) noexcept : db_(std::exchange(o.db_, nullptr)), file_(std::move(o.file_)) {}

Database& Database::operator=(Database&& o) noexcept {
    if (this != &o) {
        close();
        db_ = std::exchange(o.db_, nullptr);
        file_ = std::move(o.file_);
    }
    return *this;
}

void Database::close() {
    if (db_) sqlite3_close_v2(db_);  // unfinalized statements keep it alive until they go
    db_ = nullptr;
}

Error Database::error(std::string_view what, int rc) const {
    const int primary = rc & 0xff;
    const ErrorCode code = (primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB) ? ErrorCode::CorruptData
                           : primary == SQLITE_CANTOPEN                           ? ErrorCode::NotFound
                                                                                   : ErrorCode::Io;
    std::string msg = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
    return makeError(code, pathToUtf8(file_.filename()) + ": " + msg + " (" + std::string(what) + ")");
}

Result<Database> Database::open(const std::filesystem::path& file, Mode mode) {
    Database d;
    d.file_ = mode == Mode::Memory ? std::filesystem::path(":memory:") : file;
    std::string name;
    int flags = 0;
    switch (mode) {
    case Mode::ReadWrite: {
        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
        name = pathToUtf8(file);
        flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        break;
    }
    case Mode::ReadOnly: {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec))
            return makeError(ErrorCode::NotFound, pathToUtf8(file) + ": no such database");
        name = fileUri(file) + "?immutable=1";
        flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_URI;
        break;
    }
    case Mode::Memory:
        name = ":memory:";
        flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY;
        break;
    }
    const int rc = sqlite3_open_v2(name.c_str(), &d.db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        Error e = d.error("open", rc);
        d.close();
        return e;
    }
    // The first real access also tells a non-database file apart (SQLITE_NOTADB -> CorruptData).
    if (auto r = d.exec("PRAGMA foreign_keys=ON; PRAGMA busy_timeout=2000;"); !r) return r.error();
    if (mode == Mode::ReadWrite) {
        if (auto r = d.exec("PRAGMA journal_mode=WAL"); !r) return r.error();
        if (d.journalMode() != "wal") {
            // vfat or a filesystem without shared-memory mmap (architecture doc §9).
            util::Log::warn("storage", pathToUtf8(file.filename()) + ": WAL unavailable, using journal_mode=DELETE");
            if (auto r = d.exec("PRAGMA journal_mode=DELETE"); !r) return r.error();
        }
        if (auto r = d.exec("PRAGMA synchronous=NORMAL"); !r) return r.error();
    } else if (mode == Mode::ReadOnly) {
        if (auto r = d.exec("PRAGMA schema_version"); !r) return r.error();
    }
    return d;
}

Result<void> Database::exec(std::string_view sql) {
    char* msg = nullptr;
    const std::string s(sql);
    const int rc = sqlite3_exec(db_, s.c_str(), nullptr, nullptr, &msg);
    if (msg) sqlite3_free(msg);
    if (rc != SQLITE_OK) return error(sql.substr(0, 60), rc);
    return {};
}

Result<Statement> Database::prepare(std::string_view sql) {
    sqlite3_stmt* s = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &s, nullptr);
    if (rc != SQLITE_OK) return error(sql.substr(0, 60), rc);
    return Statement(s, this);
}

Result<void> Database::transaction(const std::function<Result<void>()>& body) {
    if (auto r = exec("BEGIN IMMEDIATE"); !r) return r;
    Result<void> r = body();
    if (!r) {
        (void)exec("ROLLBACK");
        return r;
    }
    if (auto c = exec("COMMIT"); !c) {
        (void)exec("ROLLBACK");
        return c;
    }
    return {};
}

int Database::userVersion() {
    auto s = prepare("PRAGMA user_version");
    if (!s) return 0;
    auto row = s->step();
    return row && *row ? s->integer(0) : 0;
}

Result<void> Database::setUserVersion(int v) { return exec("PRAGMA user_version=" + std::to_string(v)); }

int64_t Database::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }
int Database::changes() const { return sqlite3_changes(db_); }

std::string Database::journalMode() {
    auto s = prepare("PRAGMA journal_mode");
    if (!s) return {};
    auto row = s->step();
    return row && *row ? s->text(0) : std::string();
}

// --- Migrations --------------------------------------------------------------------------------

Result<void> migrate(Database& db, std::span<const Migration> steps) {
    const int current = db.userVersion();
    const int latest = steps.empty() ? 0 : steps.back().version;
    if (current > latest)
        return makeError(ErrorCode::Unsupported, pathToUtf8(db.file().filename()) + ": schema version " +
                                                     std::to_string(current) + " is newer than this app (" +
                                                     std::to_string(latest) + ")");
    int expected = current + 1;
    for (const Migration& m : steps) {
        if (m.version <= current) continue;
        if (m.version != expected)
            return makeError(ErrorCode::InvalidArgument, "migration steps must be consecutive (missing " +
                                                             std::to_string(expected) + ")");
        auto r = db.transaction([&]() -> Result<void> {
            if (auto e = db.exec(m.sql); !e) return e;
            return db.setUserVersion(m.version);
        });
        if (!r) return r;
        util::Log::info("storage", pathToUtf8(db.file().filename()) + ": schema " + std::to_string(m.version));
        ++expected;
    }
    return {};
}

} // namespace pchess::storage
