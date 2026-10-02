// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/Importer.h"
#include "core/engine/EngineDescriptor.h"
#include "core/storage/Backup.h"
#include "core/storage/ChessDb.h"
#include "core/util/Clock.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;

namespace {

int playedGames(Application& app) { return storage::playedGameCount(*app.userDb()).value(); }

void addGame(Application& app) {
    storage::PlayedGame g;
    g.date = 1'800'000'000;
    g.pgn = "1. e4 e5 *";
    g.result = "1-0";
    g.human = "white";
    REQUIRE(storage::addPlayedGame(*app.userDb(), g).ok());
}

} // namespace

TEST_CASE("backup: a backup folder in exports/, recognised by Import, restored over newer data") {
    const auto root = std::filesystem::temp_directory_path() / "pchess-backup";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    util::FakeClock clock{1'800'000'000};
    AppConfig cfg;
    cfg.root = root;
    cfg.fontDir = test::fontDir();
    cfg.logToFile = false;
    cfg.persistSettings = true;
    cfg.clock = &clock;
    auto app = std::make_unique<Application>(host, presenter, cfg);
    app->start();

    addGame(*app);
    puzzles::Puzzle p;
    p.id = "abc";
    REQUIRE(app->progress()->record("sample", p, true, true, 1'800'000'000).ok());
    app->settings().tapToMove = false;
    app->saveSettings();

    auto dir = app->backupUserData();
    REQUIRE(dir.ok());
    CHECK(std::filesystem::exists(*dir / "chess.db"));
    CHECK(std::filesystem::exists(*dir / "progress.db"));
    CHECK(std::filesystem::exists(*dir / "backup.txt"));
    CHECK(dir->parent_path() == app->paths().exports());

    // Later: more games, another setting. The backup goes into imports/ and Import offers it.
    addGame(*app);
    addGame(*app);
    app->settings().tapToMove = true;
    app->saveSettings();
    CHECK_EQ(playedGames(*app), 3);
    const auto copy = app->paths().imports() / dir->filename();
    std::filesystem::copy(*dir, copy, ec);
    REQUIRE_FALSE(ec);
    auto items = Importer(app->paths(), engine::currentPlatformKey()).scan({});
    REQUIRE_EQ(items.size(), size_t(1));
    CHECK(items[0].kind == ImportKind::Backup);
    CHECK(items[0].valid);
    CHECK(items[0].detail.find("created") == std::string::npos);  // just the date, after a ·

    clock.advance(std::chrono::seconds(5));  // the safety backup gets its own folder name
    auto previous = app->restoreUserData(copy);
    REQUIRE(previous.ok());
    CHECK_EQ(playedGames(*app), 1);
    CHECK_FALSE(app->settings().tapToMove);
    REQUIRE(app->progress()->state("abc").value().has_value());
    CHECK(std::filesystem::exists(*previous / "chess.db"));  // the data replaced: 3 games
    {
        auto old = storage::Database::open(*previous / "chess.db", storage::Database::Mode::ReadOnly);
        REQUIRE(old.ok());
        CHECK_EQ(storage::playedGameCount(*old).value(), 3);
    }
    app.reset();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("backup: a database from a newer app or not from PocketChess is refused") {
    const auto dir = std::filesystem::temp_directory_path() / "pchess-backup-check";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    {
        auto db = storage::Database::open(dir / "newer.db", storage::Database::Mode::ReadWrite);
        REQUIRE(db.ok());
        REQUIRE(db->setUserVersion(99).ok());
        auto plain = storage::Database::open(dir / "plain.db", storage::Database::Mode::ReadWrite);
        REQUIRE(plain.ok());
        REQUIRE(plain->exec("CREATE TABLE t(x)").ok());
    }
    CHECK_FALSE(storage::checkBackupFile(dir / "newer.db", 7).ok());
    CHECK_FALSE(storage::checkBackupFile(dir / "plain.db", 7).ok());
    CHECK_FALSE(storage::checkBackupFile(dir / "missing.db", 7).ok());
    std::filesystem::remove_all(dir, ec);
}
