// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/storage/ChessDb.h"
#include "core/storage/Database.h"
#include "support/Test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace pchess;
using namespace pchess::storage;

namespace {

// Fresh folder under the system temp dir (testing doc §1: files only in temp dirs).
std::filesystem::path freshDir(const std::filesystem::path& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

Database memoryDb() {
    auto db = Database::open({}, Database::Mode::Memory);
    REQUIRE(db.ok());
    return std::move(*db);
}

} // namespace

TEST_CASE("storage: statements bind and read every type, including NULL, blobs and UTF-8") {
    Database db = memoryDb();
    REQUIRE(db.exec("CREATE TABLE t(i INTEGER, r REAL, s TEXT, b BLOB, n TEXT)").ok());
    auto ins = db.prepare("INSERT INTO t VALUES(?1, ?2, ?3, ?4, ?5)");
    REQUIRE(ins.ok());
    const std::vector<uint8_t> bytes = {0, 1, 2, 255};
    ins->bind(1, int64_t(1) << 40).bind(2, 0.5).bind(3, "Café ♞").bindBlob(4, bytes).bindNull(5);
    REQUIRE(ins->run().ok());
    CHECK_EQ(db.lastInsertId(), int64_t(1));
    ins->bind(1, 7).bind(2, -1.25).bind(3, std::string_view()).bindBlob(4, {}).bind(5, "x");
    REQUIRE(ins->run().ok());

    auto sel = db.prepare("SELECT i, r, s, b, n FROM t ORDER BY rowid");
    REQUIRE(sel.ok());
    auto row = sel->step();
    REQUIRE(row.ok());
    REQUIRE(*row);
    CHECK_EQ(sel->columns(), 5);
    CHECK_EQ(sel->int64(0), int64_t(1) << 40);
    CHECK_EQ(sel->real(1), 0.5);
    CHECK_EQ(sel->text(2), std::string("Café ♞"));
    CHECK(sel->blob(3) == bytes);
    CHECK(sel->isNull(4));
    row = sel->step();
    REQUIRE(*row);
    CHECK_EQ(sel->integer(0), 7);
    CHECK(sel->text(2).empty());
    CHECK_FALSE(sel->isNull(3));  // empty blob, not NULL
    CHECK(sel->blob(3).empty());
    row = sel->step();
    REQUIRE(row.ok());
    CHECK_FALSE(*row);
}

TEST_CASE("storage: SQL errors and bind errors are Results, not crashes") {
    Database db = memoryDb();
    auto bad = db.prepare("SELEC 1");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().code == ErrorCode::Io);
    CHECK(bad.error().context.find("syntax error") != std::string::npos);
    CHECK_FALSE(db.exec("INSERT INTO missing VALUES(1)").ok());

    auto s = db.prepare("SELECT ?1");
    REQUIRE(s.ok());
    s->bind(5, 1);  // out of range
    CHECK_FALSE(s->step().ok());
    s->reset();
    s->bind(1, 3);
    auto row = s->step();
    REQUIRE(row.ok());
    CHECK_EQ(s->integer(0), 3);
}

TEST_CASE("storage: a failing transaction rolls back") {
    Database db = memoryDb();
    REQUIRE(db.exec("CREATE TABLE t(v INTEGER UNIQUE)").ok());
    auto r = db.transaction([&]() -> Result<void> {
        if (auto e = db.exec("INSERT INTO t VALUES(1)"); !e) return e;
        return db.exec("INSERT INTO t VALUES(1)");  // UNIQUE violation
    });
    CHECK_FALSE(r.ok());
    auto count = db.prepare("SELECT count(*) FROM t");
    REQUIRE(count->step().ok());
    CHECK_EQ(count->integer(0), 0);

    REQUIRE(db.transaction([&]() -> Result<void> { return db.exec("INSERT INTO t VALUES(2)"); }).ok());
    count->reset();
    REQUIRE(count->step().ok());
    CHECK_EQ(count->integer(0), 1);
}

TEST_CASE("storage: migrations run once, in order, and refuse newer or broken schemas") {
    Database db = memoryDb();
    const Migration steps[] = {{1, "CREATE TABLE a(x)"}, {2, "CREATE TABLE b(y)"}};
    REQUIRE(migrate(db, std::span(steps, 1)).ok());
    CHECK_EQ(db.userVersion(), 1);
    REQUIRE(migrate(db, steps).ok());  // only step 2 runs
    CHECK_EQ(db.userVersion(), 2);
    REQUIRE(migrate(db, steps).ok());  // nothing to do
    CHECK(db.exec("INSERT INTO b VALUES(1)").ok());

    // A newer database than the app (downgrade) is refused.
    auto r = migrate(db, std::span(steps, 1));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == ErrorCode::Unsupported);

    // A failing step leaves the version and the schema untouched.
    Database db2 = memoryDb();
    const Migration broken[] = {{1, "CREATE TABLE ok(x)"}, {2, "CREATE TABLE c(z); CREATE TABL oops"}};
    CHECK_FALSE(migrate(db2, broken).ok());
    CHECK_EQ(db2.userVersion(), 1);
    CHECK_FALSE(db2.exec("INSERT INTO c VALUES(1)").ok());

    const Migration gap[] = {{1, "CREATE TABLE g(x)"}, {3, "CREATE TABLE h(x)"}};
    Database db3 = memoryDb();
    CHECK_FALSE(migrate(db3, gap).ok());
}

TEST_CASE("storage: user databases on disk use WAL, persist, and survive reopening") {
    const auto dir = freshDir("pchess-storage-test");
    const auto file = dir / "user" / "chess.db";  // parent folder is created
    {
        auto db = openChessDb(file);
        if (!db) MESSAGE(db.error().message());
        REQUIRE(db.ok());
        CHECK_EQ(db->journalMode(), std::string("wal"));
        CHECK_EQ(db->userVersion(), int(chessDbMigrations().back().version));
        REQUIRE(saveSettings(*db, {{"theme", "Paper"}, {"profile", "Balanced"}}).ok());
        REQUIRE(saveSettings(*db, {{"profile", "Eco"}}).ok());  // upsert
    }
    auto db = openChessDb(file);
    REQUIRE(db.ok());
    auto s = loadSettings(*db);
    REQUIRE(s.ok());
    CHECK_EQ(s->size(), size_t(2));
    CHECK_EQ(s->at("profile"), std::string("Eco"));
    CHECK_EQ(s->at("theme"), std::string("Paper"));
    auto ins = db->prepare("INSERT INTO played_games(date, pgn, result, mode) VALUES(1, '1. e4 *', '*', 'play')");
    REQUIRE(ins.ok());
    CHECK(ins->run().ok());
    db->close();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("storage: played games are stored and listed newest first") {
    auto db = Database::open({}, Database::Mode::Memory);
    REQUIRE(db.ok());
    REQUIRE(migrate(*db, chessDbMigrations()).ok());
    auto a = addPlayedGame(*db, {0, 100, "[Result \"1-0\"]\n\n1. e4 1-0\n", "1-0", "play", "casual", "stockfish"});
    auto b = addPlayedGame(*db, {0, 200, "1. d4 *", "*", "play", "", ""});
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(*b > *a);
    auto list = recentPlayedGames(*db, 10);
    REQUIRE(list.ok());
    REQUIRE_EQ(list->size(), size_t(2));
    CHECK_EQ((*list)[0].id, *b);
    CHECK_EQ((*list)[0].result, std::string("*"));
    CHECK((*list)[0].strength.empty());  // NULL reads back as empty
    CHECK_EQ((*list)[1].engine, std::string("stockfish"));
    CHECK_EQ((*list)[1].date, int64_t(100));
    auto one = recentPlayedGames(*db, 1);
    REQUIRE(one.ok());
    CHECK_EQ(one->size(), size_t(1));
}

TEST_CASE("storage: content databases open read-only; missing and non-database files are errors") {
    const auto dir = freshDir(u8"pchess-storage-ro ü");  // non-ASCII + space: URI encoding
    const auto file = dir / "pack #1.db";
    {
        auto db = Database::open(file, Database::Mode::ReadWrite);
        REQUIRE(db.ok());
        REQUIRE(db->exec("PRAGMA journal_mode=DELETE; CREATE TABLE p(id); INSERT INTO p VALUES(42);").ok());
    }
    auto ro = Database::open(file, Database::Mode::ReadOnly);
    if (!ro) MESSAGE(ro.error().message());
    REQUIRE(ro.ok());
    auto q = ro->prepare("SELECT id FROM p");
    REQUIRE(q.ok());
    REQUIRE(q->step().ok());
    CHECK_EQ(q->integer(0), 42);
    CHECK_FALSE(ro->exec("INSERT INTO p VALUES(1)").ok());
    ro->close();

    auto missing = Database::open(dir / "nope.db", Database::Mode::ReadOnly);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);

    const auto junk = dir / "junk.db";
    {
        std::ofstream(junk, std::ios::binary) << std::string(4096, 'x');
    }
    auto bad = Database::open(junk, Database::Mode::ReadOnly);
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().code == ErrorCode::CorruptData);
    auto badRw = openChessDb(junk);
    REQUIRE_FALSE(badRw.ok());
    CHECK(badRw.error().code == ErrorCode::CorruptData);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
