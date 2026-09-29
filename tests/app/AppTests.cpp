// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "core/chess/Fen.h"
#include "core/chess/San.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>
#include <fstream>

using namespace pchess;
using namespace pchess::app;
using namespace pchess::ui;

namespace {

// A headless app on a temporary root; fonts come from the repo (desktop) or the bundle (device).
struct Harness {
    HeadlessHost host;
    HeadlessPresenter presenter;
    std::unique_ptr<Application> app;

    explicit Harness(AppConfig cfg = {}, platform::DeviceProfile profile = platform::DeviceProfile::verseProColor())
        : presenter(profile) {
        cfg.root = std::filesystem::temp_directory_path() / "pocketchess-tests";
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = false;  // one shared temp root: no settings may leak between tests
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    AnalyzeScreen* analyze() const { return dynamic_cast<AnalyzeScreen*>(app->top()); }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
    }
    const PresentRequest& last() const { return presenter.requests.back(); }
};

AppConfig analyzeConfig(const std::string& fen = "", bool mono = false) {
    AppConfig c;
    c.startScreen = "analyze";
    c.fen = fen;
    c.mono = mono;
    return c;
}

} // namespace

TEST_CASE("app: starts on Home with one full refresh") {
    Harness h;
    REQUIRE(h.app->top() != nullptr);
    CHECK_EQ(std::string(h.app->top()->name()), std::string("home"));
    REQUIRE_EQ(h.presenter.requests.size(), size_t(1));
    CHECK(h.last().mode == RefreshMode::Full);
    test::checkGolden("home", h.app->canvas());
}

TEST_CASE("app: Home keys move the focus with fast refreshes and open Analyze") {
    Harness h;
    h.run("key:next");
    CHECK(h.last().mode == RefreshMode::Fast);
    h.run("key:prev;key:ok");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("analyze"));
    CHECK(h.last().mode == RefreshMode::Full);
    h.run("key:back");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("home"));
}

TEST_CASE("app: tap-to-move refreshes only the affected squares") {
    Harness h(analyzeConfig());
    const Rect board = h.app->layout().board;
    const int sq = h.app->layout().square;

    h.run("tap:e2");  // select: e2 + e3 + e4 markers
    CHECK(h.last().mode == RefreshMode::Fast);
    for (const Rect& r : h.last().rects) CHECK(board.contains(r));
    int64_t area = 0;
    for (const Rect& r : h.last().rects) area += r.area();
    CHECK(area <= int64_t{3} * sq * sq);

    h.run("tap:e4");  // move: e2, e3, e4 on the board + the panel text
    CHECK(h.last().mode == RefreshMode::Partial);
    int64_t boardArea = 0;
    for (const Rect& r : h.last().rects) boardArea += r.intersected(board).area();
    CHECK(boardArea <= int64_t{3} * sq * sq);
    CHECK_EQ(chess::toFen(h.analyze()->position()),
             std::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
}

TEST_CASE("app: full refresh every N content updates (ghosting cleanup)") {
    Harness h(analyzeConfig());
    h.app->settings().fullRefreshEvery = 4;
    h.app->applySettings();
    h.app->flush();
    int fullsBefore = h.app->counters().full;
    h.run("drag:g1-f3;drag:g8-f6;drag:f3-g1;drag:f6-g8");
    CHECK_EQ(h.app->counters().full - fullsBefore, 1);
}

TEST_CASE("app: back/forward, variations and drag-to-move") {
    Harness h(analyzeConfig());
    h.run("drag:e2-e4;drag:e7-e5;drag:g1-f3");
    auto* a = h.analyze();
    REQUIRE(a != nullptr);
    CHECK_EQ(a->tree().mainLine().size(), size_t(3));
    h.run("key:prev;key:prev");
    CHECK_EQ(a->position().fullmoveNumber(), 1);
    CHECK(a->position().sideToMove() == chess::Black);
    h.run("key:next;key:next");
    CHECK_EQ(chess::toFen(a->position()).substr(0, 19), std::string("rnbqkbnr/pppp1ppp/8"));
    h.run("key:prev;drag:b1-c3");  // new variation at move 2
    CHECK_EQ(a->tree().root()->mainChild()->mainChild()->children.size(), size_t(2));
    CHECK_EQ(a->tree().mainLine().size(), size_t(3));  // the main line is kept
    h.run("drag:e2-e5");  // illegal: nothing happens
    CHECK(a->position().sideToMove() == chess::Black);
}

TEST_CASE("app: promotion dialog picks the piece") {
    Harness h(analyzeConfig("8/P6k/8/8/8/8/6K1/8 w - - 0 1"));
    h.run("tap:a7;tap:a8");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("promotion"));
    test::checkGolden("promotion", h.app->canvas());
    h.run("key:next;key:next;key:next;key:ok");  // Q R B N -> N
    CHECK_EQ(std::string(h.app->top()->name()), std::string("analyze"));
    CHECK(h.analyze()->position().pieceAt(chess::A8) == chess::WKnight);
}

TEST_CASE("app: FEN entry through the on-canvas dialog, with error for bad input") {
    Harness h(analyzeConfig());
    h.run("key:menu;key:next;key:ok;key:next;key:ok");  // menu -> Position… -> "Edit FEN"
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("text-entry"));
    std::string clear;
    for (int i = 0; i < 80; ++i) clear += "text:\b;";
    h.run(clear + "text:4k3/8/8/8/8/8/8/4K2R w K - 0 1;key:ok");
    CHECK_EQ(chess::toFen(h.analyze()->position()), std::string("4k3/8/8/8/8/8/8/4K2R w K - 0 1"));

    h.run("key:menu;key:next;key:ok;key:next;key:ok");
    h.run(clear + "text:not a fen;key:ok");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));
    h.run("key:ok");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("analyze"));
}

TEST_CASE("app: golden images of the board screens") {
    {
        Harness h(analyzeConfig());
        h.run("drag:e2-e4;drag:e7-e5;drag:g1-f3;drag:b8-c6;drag:f1-b5;drag:a7-a6;tap:b5");
        test::checkGolden("analyze-ruy-lopez", h.app->canvas());
    }
    {
        Harness h(analyzeConfig("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", true));
        h.run("tap:h5;tap:f7");
        test::checkGolden("analyze-mono-mate", h.app->canvas());
    }
    {
        Harness h(analyzeConfig("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));
        h.run("key:menu;key:next;key:ok;key:ok");  // Position… -> Flip
        test::checkGolden("analyze-kiwipete-flipped", h.app->canvas());
    }
    {
        Harness h(analyzeConfig(), *platform::DeviceProfile::byName("mono-758x1024"));
        h.run("tap:d2");
        test::checkGolden("analyze-758x1024", h.app->canvas());
    }
}

TEST_CASE("app: Analyze move actions: back to main line, promote and delete a variation, comments") {
    Harness h(analyzeConfig());
    AnalyzeScreen* a = h.analyze();
    REQUIRE(a->playSan("e4"));
    REQUIRE(a->playSan("e5"));
    a->goBack();
    REQUIRE(a->playSan("c5"));  // variation 1... c5
    CHECK_EQ(a->tree().mainLine().size(), size_t(2));

    a->goToMainLine();  // the fork point: after 1. e4
    CHECK_EQ(a->position().sideToMove(), chess::Black);
    CHECK(a->position().pieceAt(chess::parseSquare("c7")) == chess::BPawn);

    REQUIRE(a->playSan("c5"));  // back into the variation
    a->promoteVariation();
    CHECK_EQ(chess::toSan(chess::Position::startPosition(), a->tree().mainLine()[0]), std::string("e4"));
    chess::Position afterE4 = chess::Position::startPosition();
    afterE4.makeMove(a->tree().mainLine()[0]);
    CHECK_EQ(chess::toSan(afterE4, a->tree().mainLine()[1]), std::string("c5"));
    CHECK(a->position().pieceAt(chess::parseSquare("c5")) == chess::BPawn);

    a->deleteMove();  // removes 1... c5, e5 is the main line again
    CHECK_EQ(a->tree().mainLine().size(), size_t(2));
    CHECK_EQ(a->tree().root()->mainChild()->children.size(), size_t(1));

    // Menu -> Move… -> Comment… (on-canvas text entry on desktop).
    h.run("key:menu;key:ok;key:ok");
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("text-entry"));
    h.run("text:The main line;key:ok");
    CHECK_EQ(a->tree().root()->mainChild()->comment, std::string("The main line"));
    test::checkGolden("analyze-comment", h.app->canvas());
}

TEST_CASE("app: Analyze loads a game from a PGN file in imports/") {
    Harness h(analyzeConfig());
    const auto imports = h.app->paths().imports();
    std::error_code ec;
    std::filesystem::create_directories(imports, ec);
    for (const auto& de : std::filesystem::directory_iterator(imports, ec)) std::filesystem::remove(de.path(), ec);
    {
        std::ofstream(imports / "two.pgn", std::ios::binary)
            << "[White \"Morphy\"]\n[Black \"Duke\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 d6 1-0\n\n"
               "[White \"Anderssen\"]\n[Black \"Kieseritzky\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. f4 {gambit} exf4 1-0\n\n"
               "[White \"Broken\"]\n\n1. e4 e4 *\n";
    }
    h.analyze()->loadPgnFromImports();
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("message"));  // files
    h.run("key:ok");                                                        // two.pgn
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("message"));  // games (the broken one skipped)
    h.run("key:next;key:ok");                                               // Anderssen – Kieseritzky
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("analyze"));
    AnalyzeScreen* a = h.analyze();
    CHECK_EQ(a->tree().mainLine().size(), size_t(4));
    CHECK(a->position() == chess::Position::startPosition());  // at the start, ready to step through
    CHECK_EQ(a->tree().root()->mainChild()->mainChild()->mainChild()->comment, std::string("gambit"));
    std::filesystem::remove(imports / "two.pgn", ec);

    // An empty imports/ explains where to put files.
    a->loadPgnFromImports();
    CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));
}
