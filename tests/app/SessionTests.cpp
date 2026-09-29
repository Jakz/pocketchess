// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/PlayScreen.h"
#include "app/Script.h"
#include "core/storage/ChessDb.h"
#include "support/FakeEngine.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;

namespace {

// One "run" of the app on a persistent root: destroying it and making another one is a restart.
struct AppRun {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    test::FakeEngineFactory fake;
    std::unique_ptr<Application> app;

    explicit AppRun(const std::filesystem::path& root) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.extraEngines.push_back(test::fakeDescriptor("fakefish"));
        cfg.engineFactory = [this](const engine::EngineDescriptor& d, util::Scheduler& s) {
            return std::make_unique<engine::UciEngine>(d, s, test::selfExe(), fake.factory());
        };
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
        pump();
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
    PlayScreen* play() const { return dynamic_cast<PlayScreen*>(app->top()); }
    AnalyzeScreen* analyze() const { return dynamic_cast<AnalyzeScreen*>(app->top()); }
    std::map<std::string, std::string> session() const {
        auto s = storage::loadSession(*app->userDb());
        REQUIRE(s.ok());
        return *s;
    }
};

std::filesystem::path freshRoot(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return root;
}

} // namespace

TEST_CASE("session: a game in progress is offered for resume after a restart") {
    const auto root = freshRoot("pchess-session-play");
    {
        AppRun r(root);
        CHECK_EQ(r.top(), std::string("home"));  // nothing to resume yet
        r.app->push(std::make_unique<PlayScreen>(*r.app, PlaySetup{chess::White, "beginner", ""}));
        REQUIRE(r.play()->playSan("e4"));
        r.pump();
        REQUIRE_EQ(r.play()->tree().mainLine().size(), size_t(2));
        const auto saved = r.session();
        CHECK_EQ(saved.at("screen"), std::string("play"));
        CHECK_EQ(saved.at("play.moves"), std::string("e2e4 a7a5"));
    }  // killed: no exit event
    {
        AppRun r(root);
        REQUIRE_EQ(r.top(), std::string("message"));  // Resume? [Home] [Resume]
        r.run("key:ok");                              // "Resume" has the focus
        REQUIRE(r.play() != nullptr);
        CHECK_EQ(r.play()->tree().mainLine().size(), size_t(2));
        CHECK_EQ(r.play()->setup().strength, std::string("beginner"));
        CHECK_FALSE(r.play()->engineThinking());
        REQUIRE(r.play()->playSan("d4"));  // the game goes on
        r.pump();
        CHECK_EQ(r.play()->tree().mainLine().size(), size_t(4));
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("session: killed while the engine thinks, the engine moves again after resume") {
    const auto root = freshRoot("pchess-session-thinking");
    {
        AppRun r(root);
        r.app->push(std::make_unique<PlayScreen>(*r.app, PlaySetup{chess::White, "beginner", ""}));
        r.fake.behaviour.bestmoveOnMovetime = false;  // the engine never answers in this run
        REQUIRE(r.play()->playSan("e4"));
        r.pump();
        CHECK(r.play()->engineThinking());
    }
    {
        AppRun r(root);
        r.run("key:ok");
        REQUIRE(r.play() != nullptr);
        r.pump();
        CHECK_FALSE(r.play()->engineThinking());
        CHECK_EQ(r.play()->tree().mainLine().size(), size_t(2));
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("session: Home discards it; a finished game leaves nothing to resume") {
    const auto root = freshRoot("pchess-session-home");
    {
        AppRun r(root);
        r.app->push(std::make_unique<PlayScreen>(*r.app, PlaySetup{chess::White, "beginner", ""}));
        REQUIRE(r.play()->playSan("e4"));
        r.pump();
    }
    {
        AppRun r(root);
        REQUIRE_EQ(r.top(), std::string("message"));
        r.run("key:prev;key:ok");  // "Home"
        CHECK_EQ(r.top(), std::string("home"));
        CHECK(r.session().empty());
    }
    {
        AppRun r(root);
        CHECK_EQ(r.top(), std::string("home"));
        r.app->push(std::make_unique<PlayScreen>(*r.app, PlaySetup{chess::White, "beginner", ""}));
        REQUIRE(r.play()->playSan("e4"));
        r.pump();
        CHECK_FALSE(r.session().empty());
        r.play()->resign();
        CHECK(r.session().empty());  // finished: saved in played_games instead
    }
    {
        AppRun r(root);
        CHECK_EQ(r.top(), std::string("home"));
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("session: the analysis board comes back with its variations, current move and orientation") {
    const auto root = freshRoot("pchess-session-analyze");
    size_t nodes = 0;
    {
        AppRun r(root);
        const std::string italian = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
        r.app->push(std::make_unique<AnalyzeScreen>(*r.app, italian));
        AnalyzeScreen* a = r.analyze();
        REQUIRE(a->playSan("Bb5"));
        REQUIRE(a->playSan("a6"));
        a->goBack();
        a->goBack();
        REQUIRE(a->playSan("Bc4"));  // a variation
        a->flip();
        nodes = a->tree().nodeCount();
        const auto saved = r.session();
        CHECK_EQ(saved.at("screen"), std::string("analyze"));
    }
    {
        AppRun r(root);
        r.run("key:ok");
        AnalyzeScreen* a = r.analyze();
        REQUIRE(a != nullptr);
        CHECK_EQ(a->tree().nodeCount(), nodes);
        CHECK_EQ(a->tree().mainLine().size(), size_t(2));  // Bb5 a6 stays the main line
        CHECK_EQ(a->position().sideToMove(), chess::Black);  // after the variation move Bc4
        CHECK(a->position().pieceAt(chess::parseSquare("c4")) != chess::NoPiece);
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("session: an invalid saved session is discarded") {
    const auto root = freshRoot("pchess-session-bad");
    {
        AppRun r(root);
        REQUIRE(storage::saveSession(*r.app->userDb(), {{"screen", "play"}, {"play.human", "purple"}}).ok());
    }
    {
        AppRun r(root);
        r.run("key:ok");  // Resume: cannot be restored
        CHECK_EQ(r.top(), std::string("home"));
        CHECK(r.session().empty());
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
