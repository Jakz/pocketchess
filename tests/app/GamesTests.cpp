// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/GameStudyScreen.h"
#include "app/GamesScreen.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "core/chess/Fen.h"
#include "core/chess/San.h"
#include "core/games/GamePack.h"
#include "core/storage/ChessDb.h"
#include "core/util/Files.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;

namespace {

std::string pgnText(const char* name) {
    std::filesystem::path file = std::filesystem::path(test::fixtureDir()) / name;
    if (!std::filesystem::exists(file))
        file = std::filesystem::path(test::fontDir()).parent_path() / "samples" / "pgn" / name;
    auto t = util::readTextFile(file);
    REQUIRE(t.ok());
    return *t;
}

// <root>/content/games/sample.db: the 39 Lichess fixture games and the three classics.
std::filesystem::path gamesRoot(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto file = root / "content" / "games" / "sample.db";
    std::filesystem::create_directories(file.parent_path(), ec);
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    REQUIRE(db->exec("PRAGMA journal_mode=DELETE").ok());
    games::GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    for (const char* f : {"games-sample.pgn", "classics.pgn"})
        for (auto& g : chess::parsePgn(pgnText(f))) REQUIRE(imp.add(*g).ok());
    REQUIRE(imp.finish({{"title", "Sample games"}}).ok());
    return root;
}

struct GamesHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    std::unique_ptr<Application> app;

    explicit GamesHarness(const std::filesystem::path& root, bool persist = false) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = persist;
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    GamesScreen* browser() const { return dynamic_cast<GamesScreen*>(app->top()); }
    GameStudyScreen* study() const { return dynamic_cast<GameStudyScreen*>(app->top()); }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
        app->flush();
    }
    std::string top() const { return app->top() ? app->top()->name() : ""; }
};

} // namespace

TEST_CASE("games browser: pages of the pack, partial refreshes on page turns, filters") {
    const auto root = gamesRoot("pchess-games-browser");
    GamesHarness h(root);
    h.app->push(std::make_unique<GamesScreen>(*h.app));
    h.app->flush();
    GamesScreen* g = h.browser();
    REQUIRE(g != nullptr);
    CHECK_EQ(g->total(), 42);
    CHECK(g->pageCount() >= 5);
    REQUIRE_FALSE(g->rows().empty());
    CHECK(g->rows()[0].date.rfind("2012", 0) == 0);  // newest first (the fixture is from 2012.12.31)
    test::checkGolden("games", h.app->canvas());

    const size_t before = h.presenter.requests.size();
    h.run("key:next");
    CHECK_EQ(g->page(), 1);
    REQUIRE(h.presenter.requests.size() > before);
    CHECK(h.presenter.requests.back().mode != ui::RefreshMode::Full);
    h.run("key:prev;key:prev");
    CHECK_EQ(g->page(), 0);

    games::GameFilter f;
    f.player = "anderssen";
    g->setFilter(f);
    CHECK_EQ(g->total(), 2);
    CHECK_EQ(g->pageCount(), 1);
    f.opponent = "Kieseritzky";
    g->setFilter(f);
    REQUIRE_EQ(g->total(), 1);
    games::GameFilter old;
    old.yearTo = 1899;
    g->setFilter(old);
    CHECK_EQ(g->total(), 3);
    games::GameFilter none;
    none.player = "nobody at all";
    g->setFilter(none);
    CHECK_EQ(g->total(), 0);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("game study: stepping, jumping, variations, favorites and Analyze from here") {
    const auto root = gamesRoot("pchess-games-study");
    GamesHarness h(root, true);  // favorites live in chess.db
    h.app->push(std::make_unique<GamesScreen>(*h.app));
    games::GameFilter f;
    f.player = "kieseritzky";
    h.browser()->setFilter(f);
    REQUIRE_EQ(h.browser()->rows().size(), size_t(1));
    h.browser()->open(0);
    h.app->flush();
    GameStudyScreen* s = h.study();
    REQUIRE(s != nullptr);
    REQUIRE(s->loaded());
    CHECK(s->node()->isRoot());
    h.run("key:next;key:next;key:next");
    CHECK_EQ(s->node()->ply, 3);
    h.run("key:prev");
    CHECK_EQ(s->node()->ply, 2);

    // Tap a move in the list: jumps there.
    REQUIRE(s->moveHits().size() > 5);
    const MoveHit hit = s->moveHits()[5];
    const ui::Point c = hit.rect.center();
    h.run("tap:" + std::to_string(c.x) + "," + std::to_string(c.y));
    CHECK(s->node() == hit.node);

    // 17... Qxb2 has the variation 18. Be3 (with a comment).
    const chess::GameNode* n = s->tree().root();
    for (int i = 0; i < 34; ++i) n = n->mainChild();
    s->jumpTo(n);
    REQUIRE_EQ(n->children.size(), size_t(2));
    s->enterVariation(1);
    CHECK_FALSE(s->node()->isMainLine());
    CHECK_EQ(s->node()->comment, std::string("is the quiet alternative."));
    h.app->flush();
    test::checkGolden("game-study", h.app->canvas());
    s->mainLine();
    CHECK(s->node() == n);
    s->enterVariation(1);

    // ★ and the favorites filter.
    CHECK_FALSE(s->favorite());
    s->toggleFavorite();
    CHECK(s->favorite());
    REQUIRE(h.app->userDb() != nullptr);
    CHECK_EQ(storage::favoriteIds(*h.app->userDb(), "sample").value().size(), size_t(1));

    // Analyze from here: the same node, variation included.
    h.run("key:menu");
    REQUIRE_EQ(h.top(), std::string("message"));
    h.run("key:ok");  // "Analyze from here" (no "Variations…" entry here: the node has none)
    auto* a = dynamic_cast<AnalyzeScreen*>(h.app->top());
    REQUIRE(a != nullptr);
    CHECK(chess::toFen(a->position()) == chess::toFen(s->position()));
    h.run("key:back");
    REQUIRE(h.study() != nullptr);
    h.run("key:back");
    REQUIRE(h.browser() != nullptr);
    h.browser()->setFilter({}, true);  // favorites only
    CHECK_EQ(h.browser()->total(), 1);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("page buttons: Settings -> Input -> Swapped turns Prev into Next and back") {
    const auto root = gamesRoot("pchess-games-swap");
    GamesHarness h(root);
    h.app->push(std::make_unique<GamesScreen>(*h.app));
    h.app->flush();
    h.app->settings().swapPageKeys = true;
    h.run("key:prev");
    CHECK_EQ(h.browser()->page(), 1);
    h.run("key:next");
    CHECK_EQ(h.browser()->page(), 0);
    h.app->settings().swapPageKeys = false;
    h.run("key:next");
    CHECK_EQ(h.browser()->page(), 1);
}

TEST_CASE("games: keys only - Menu opens a game from the page, the study menu goes back") {
    const auto root = gamesRoot("pchess-games-keys");
    GamesHarness h(root);
    h.app->push(std::make_unique<GamesScreen>(*h.app));
    h.app->flush();
    h.run("key:menu;key:ok");  // "Open a game…"
    h.run("key:next;key:ok");  // the second row of the page
    REQUIRE(h.study() != nullptr);
    h.run("key:menu");
    // "Back to the list" is the entry before "Close".
    for (int i = 0; i < 2; ++i) h.run("key:prev");
    h.run("key:ok");
    CHECK(h.browser() != nullptr);
}
