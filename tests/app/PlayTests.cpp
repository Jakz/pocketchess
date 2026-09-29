// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/PlayScreen.h"
#include "app/Script.h"
#include "core/storage/ChessDb.h"
#include "support/FakeEngine.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;

namespace {

// Headless app with the scripted FakeUci engine (it plays the first legal move in UCI order).
struct PlayHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    test::FakeEngineFactory fake;
    std::unique_ptr<Application> app;

    explicit PlayHarness(bool withEngine = true, const std::filesystem::path& root = {}) {
        AppConfig cfg;
        cfg.root = root.empty() ? std::filesystem::temp_directory_path() / "pocketchess-tests" : root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = !root.empty();
        if (withEngine) cfg.extraEngines.push_back(test::fakeDescriptor("fakefish"));
        cfg.engineFactory = [this](const engine::EngineDescriptor& d, util::Scheduler& s) {
            return std::make_unique<engine::UciEngine>(d, s, test::selfExe(), fake.factory());
        };
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    PlayScreen* play() const { return dynamic_cast<PlayScreen*>(app->top()); }
    PlayScreen* start(chess::Color human, const std::string& fen = "", const std::string& strength = "casual") {
        app->push(std::make_unique<PlayScreen>(*app, PlaySetup{human, strength, fen}));
        pump();
        return play();
    }
    void pump() {
        host.advance(0ms);
        app->flush();
    }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
        pump();
    }
    std::string top() const { return app->top() ? app->top()->name() : ""; }
};

} // namespace

TEST_CASE("play: the engine replies, undo takes back the move pair") {
    PlayHarness h;
    PlayScreen* p = h.start(chess::White);
    REQUIRE(p != nullptr);
    CHECK_FALSE(p->engineThinking());
    CHECK_FALSE(p->playSan("e5"));  // illegal for White
    REQUIRE(p->playSan("e4"));
    h.pump();
    CHECK_FALSE(p->engineThinking());
    CHECK_EQ(p->tree().mainLine().size(), size_t(2));  // 1. e4 a5 (the fake's first move)
    CHECK_EQ(p->position().sideToMove(), chess::White);
    CHECK_FALSE(p->playSan("a5"));  // not Black's turn for the human
    test::checkGolden("play", h.app->canvas());

    REQUIRE(p->playSan("d4"));
    h.pump();
    CHECK_EQ(p->tree().mainLine().size(), size_t(4));
    p->undo();
    h.pump();
    CHECK_EQ(p->tree().mainLine().size(), size_t(2));
    p->undo();
    CHECK_EQ(p->tree().mainLine().size(), size_t(0));
    p->undo();  // nothing left: no-op
    CHECK_EQ(p->tree().mainLine().size(), size_t(0));
    CHECK_FALSE(p->finished());
}

TEST_CASE("play: as Black the engine opens; bestmove jobs use the chosen strength") {
    PlayHarness h;
    PlayScreen* p = h.start(chess::Black, "", "beginner");
    REQUIRE(p != nullptr);
    CHECK_EQ(p->tree().mainLine().size(), size_t(1));
    CHECK_EQ(p->position().sideToMove(), chess::Black);
    CHECK_EQ(h.fake.count("go movetime 80"), 1);  // the fake descriptor's "beginner" level
    p->undo();  // only the engine's move: nothing of ours to take back
    CHECK_EQ(p->tree().mainLine().size(), size_t(1));
}

TEST_CASE("play: checkmate ends the game, saves it and opens Analyze on it") {
    const auto root = std::filesystem::temp_directory_path() / "pchess-play-save";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    {
        PlayHarness h(true, root);
        PlayScreen* p = h.start(chess::White, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
        REQUIRE(p != nullptr);
        REQUIRE(p->playSan("Ra8#"));
        h.pump();
        CHECK(p->finished());
        CHECK_EQ(p->result(), std::string("1-0"));
        CHECK_EQ(p->reason(), std::string("Checkmate"));
        CHECK(p->savedId() > 0);
        CHECK_FALSE(p->engineThinking());
        REQUIRE_EQ(h.top(), std::string("message"));  // result dialog

        storage::Database* db = h.app->userDb();
        REQUIRE(db != nullptr);
        auto games = storage::recentPlayedGames(*db, 5);
        REQUIRE(games.ok());
        REQUIRE_EQ(games->size(), size_t(1));
        const std::string& pgn = (*games)[0].pgn;
        CHECK(pgn.find("[FEN \"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1\"]") != std::string::npos);
        CHECK(pgn.find("[White \"You\"]") != std::string::npos);
        CHECK(pgn.find("Ra8#") != std::string::npos);
        CHECK_EQ((*games)[0].result, std::string("1-0"));
        CHECK_EQ((*games)[0].strength, std::string("casual"));

        h.run("key:ok");  // "Analyze" is focused first
        REQUIRE_EQ(h.top(), std::string("analyze"));
        auto* a = dynamic_cast<AnalyzeScreen*>(h.app->top());
        REQUIRE(a != nullptr);
        CHECK_EQ(a->tree().mainLine().size(), size_t(1));
        CHECK(a->position().inCheck());
    }
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("play: resign, draw claims and leaving a game") {
    PlayHarness h;
    PlayScreen* p = h.start(chess::White);
    REQUIRE(p != nullptr);
    CHECK_FALSE(p->claimDraw());  // nothing to claim
    REQUIRE(p->playSan("Nf3"));
    h.pump();
    p->resign();
    CHECK(p->finished());
    CHECK_EQ(p->result(), std::string("0-1"));
    CHECK_EQ(p->reason(), std::string("You resigned"));
    CHECK_EQ(p->savedId(), int64_t(0));  // settings not persisted: no chess.db
    CHECK(p->pgn().find("[Result \"0-1\"]") != std::string::npos);
    h.run("key:back");  // close the result dialog
    CHECK_EQ(h.top(), std::string("play"));
    h.run("key:back");  // a finished game leaves without asking
    CHECK_EQ(h.top(), std::string("home"));

    // An unfinished game asks first.
    p = h.start(chess::White);
    REQUIRE(p->playSan("e4"));
    h.pump();
    h.run("key:back");
    REQUIRE_EQ(h.top(), std::string("message"));
    h.run("key:ok");  // focus starts on "Stay"
    CHECK_EQ(h.top(), std::string("play"));
    h.run("key:back;key:prev;key:ok");  // "Leave"
    CHECK_EQ(h.top(), std::string("home"));
}

TEST_CASE("play: a missing engine shows its dialog once and Home leaves") {
    PlayHarness h(false);
    h.start(chess::Black);  // the engine must move first
    REQUIRE_EQ(h.top(), std::string("message"));
    h.run("key:ok");  // focus starts on "Home"
    CHECK_EQ(h.top(), std::string("home"));
}
