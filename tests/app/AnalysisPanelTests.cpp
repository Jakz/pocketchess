// SPDX-License-Identifier: GPL-3.0-or-later
// Analyze screen engine panel (M2) against the in-process fake engine.
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "app/SettingsScreen.h"
#include "core/chess/Fen.h"
#include "support/FakeEngine.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <cctype>
#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace pchess::ui;
using namespace std::chrono_literals;
using Mode = AnalyzeScreen::EngineMode;

namespace {

struct EngineHarness {
    HeadlessHost host;
    HeadlessPresenter presenter;
    test::FakeEngineFactory fake;
    std::unique_ptr<Application> app;

    explicit EngineHarness(bool withEngine = true, const std::string& fen = "") {
        AppConfig cfg;
        cfg.root = std::filesystem::temp_directory_path() / "pocketchess-tests";
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = false;  // one shared temp root: no settings may leak between tests
        cfg.startScreen = "analyze";
        cfg.fen = fen;
        if (withEngine) cfg.extraEngines.push_back(test::fakeDescriptor("fakefish"));
        cfg.engineFactory = [this](const engine::EngineDescriptor& d, util::Scheduler& s) {
            return std::make_unique<engine::UciEngine>(d, s, test::selfExe(), fake.factory());
        };
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    AnalyzeScreen* analyze() const { return dynamic_cast<AnalyzeScreen*>(app->top()); }
    // One event-loop turn: posted functions and due timers, then a flush (like the hosts do).
    void pump(std::chrono::milliseconds ms = 0ms) {
        host.advance(ms);
        app->flush();
    }
    // "tap:engine" etc. for the Analyze buttons (by label, case-insensitive prefix).
    void tapButton(const std::string& label) {
        REQUIRE(analyze() != nullptr);
        for (const ui::Button& b : analyze()->buttons()) {
            std::string l = b.label;
            for (char& ch : l) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (l.rfind(label, 0) == 0) {
                const ui::Point c = b.rect.center();
                run("tap:" + std::to_string(c.x) + "," + std::to_string(c.y));
                return;
            }
        }
        REQUIRE(false);
    }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
    }
};

} // namespace

TEST_CASE("analysis panel: Quick analysis shows eval, best move and PV with throttled refreshes") {
    EngineHarness h;
    REQUIRE(h.analyze() != nullptr);
    CHECK(h.app->engines().available(engine::Role::Analysis));
    h.analyze()->setEngineMode(Mode::Quick);
    h.app->flush();
    CHECK(h.analyze()->engineMode() == Mode::Quick);
    CHECK(h.analyze()->engineJobRunning());
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("Engine: starting…"));
    const size_t presents = h.presenter.requests.size();

    h.pump();  // the fake answers at once, but the panel waits for the 1 s throttle
    CHECK_FALSE(h.analyze()->engineJobRunning());
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("Engine: starting…"));
    CHECK_EQ(h.presenter.requests.size(), presents);
    h.pump(999ms);
    CHECK_EQ(h.presenter.requests.size(), presents);
    h.pump(1ms);
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("+0.20   a3   depth 3"));
    CHECK_EQ(h.analyze()->enginePv(), std::string("1. a3 a5"));
    REQUIRE_EQ(h.presenter.requests.size(), presents + 1);
    CHECK(h.presenter.requests.back().mode != RefreshMode::Full);
    CHECK_EQ(h.fake.count("go movetime 1500"), 1);  // Balanced Quick budget
    CHECK_EQ(h.host.backgroundActivity(), 1);       // process alive (idle timeout pending)

    // A move restarts the analysis for the new position; the evaluation stays from White's view.
    REQUIRE(h.analyze()->playSan("e4"));
    h.app->flush();
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("Engine: thinking…"));
    h.pump();
    h.pump(1s);
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("-0.20   a5   depth 3"));
    CHECK_EQ(h.analyze()->enginePv(), std::string("1… a5 2. a3"));
    CHECK_EQ(h.fake.spawned, 1);
    CHECK_EQ(h.fake.count("position startpos moves e2e4"), 1);

    test::checkGolden("analyze-engine-quick", h.app->canvas());

    // Off: no job, the panel lines disappear.
    h.analyze()->setEngineMode(Mode::Off);
    h.app->flush();
    CHECK_FALSE(h.analyze()->engineJobRunning());
    CHECK(h.analyze()->engineHeadline().empty());
    // Balanced idle timeout: the process quits after 60 s.
    h.pump(61s);
    CHECK_FALSE(h.app->engines().processRunning());
    CHECK_EQ(h.host.backgroundActivity(), 0);
}

TEST_CASE("analysis panel: Deep analysis, leaving the screen, sleep and dialogs stop the engine") {
    EngineHarness h;
    h.tapButton("engine");
    CHECK(h.analyze()->engineMode() == Mode::Quick);
    h.tapButton("engine");
    CHECK(h.analyze()->engineMode() == Mode::Deep);
    h.pump(1s);
    CHECK_EQ(h.fake.count("go depth 22 movetime 30000"), 1);  // Balanced Deep auto-stop

    // Another Deep search that keeps running (the fake waits for stop on infinite searches):
    // switch to Performance, where Deep has no auto-stop.
    h.app->settings().powerProfile = engine::PowerProfileId::Performance;
    h.app->applySettings();
    h.analyze()->setEngineMode(Mode::Deep);
    h.pump();
    CHECK_EQ(h.fake.count("go infinite"), 1);
    CHECK(h.analyze()->engineJobRunning());

    // The menu dialog covers the screen: the search stops, and resumes when it closes.
    h.run("key:menu");
    h.pump();
    CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));
    CHECK_EQ(h.fake.count("stop"), 1);
    h.run("key:back");
    h.pump();
    CHECK_EQ(std::string(h.app->top()->name()), std::string("analyze"));
    CHECK(h.analyze()->engineJobRunning());
    CHECK_EQ(h.fake.count("go infinite"), 2);

    // Sleep: engine off at once, mode back to Off.
    h.app->handleLifecycle(platform::Lifecycle::Sleep);
    h.pump();
    CHECK(h.analyze()->engineMode() == Mode::Off);
    CHECK_FALSE(h.app->engines().processRunning());
    CHECK_EQ(h.host.backgroundActivity(), 0);
    h.app->handleLifecycle(platform::Lifecycle::Wake);
    h.pump();
    CHECK_FALSE(h.app->engines().processRunning());

    // Leaving the screen cancels the job.
    h.analyze()->setEngineMode(Mode::Deep);
    h.pump();
    CHECK(h.app->engines().busy());
    h.run("key:back");
    h.pump();
    CHECK_EQ(std::string(h.app->top()->name()), std::string("home"));
    CHECK_FALSE(h.app->engines().busy());
}

TEST_CASE("analysis panel: engine missing shows a dialog and everything keeps working") {
    EngineHarness h(false);
    CHECK_FALSE(h.app->engines().available(engine::Role::Analysis));
    h.tapButton("engine");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("message"));
    test::checkGolden("analyze-engine-missing", h.app->canvas());
    h.run("key:ok");  // Continue (the primary button)
    REQUIRE(h.analyze() != nullptr);
    CHECK(h.analyze()->engineMode() == Mode::Off);
    CHECK(h.analyze()->playSan("d4"));
    CHECK_EQ(h.fake.spawned, 0);
}

TEST_CASE("analysis panel: checkmate needs no engine; a crash is reported in the panel") {
    EngineHarness h(true, "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    h.analyze()->setEngineMode(Mode::Quick);
    h.app->flush();
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("Checkmate"));
    CHECK_EQ(h.fake.spawned, 0);

    h.fake.behaviour.crashOnGo = true;  // applies to the process spawned next
    REQUIRE(h.analyze()->loadFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    h.pump(1s);
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("Engine error"));
    CHECK(h.analyze()->enginePv().find("exited") != std::string::npos);
    CHECK_EQ(h.host.backgroundActivity(), 0);
}

TEST_CASE("analysis panel: MultiPV shows the extra lines under the main line") {
    EngineHarness h;
    h.fake.behaviour.extraOptions = {"option name MultiPV type spin default 1 min 1 max 500"};
    h.app->settings().analysisMultiPv = 3;  // Settings -> Analysis lines
    h.analyze()->setEngineMode(Mode::Quick);
    h.pump();
    h.pump(1s);
    CHECK_EQ(h.fake.count("setoption name MultiPV value 3"), 1);
    CHECK_EQ(h.analyze()->engineHeadline(), std::string("+0.20   a3   depth 3"));
    REQUIRE_EQ(h.analyze()->engineMoreLines().size(), size_t(2));
    CHECK_EQ(h.analyze()->engineMoreLines()[0], std::string("2) +0.10  a4 a5"));
    CHECK_EQ(h.analyze()->engineMoreLines()[1], std::string("3) +0.00  Na3 a5"));
    test::checkGolden("analyze-multipv", h.app->canvas());

    // Back to one line: the panel shrinks when the screen comes back from Settings.
    h.app->settings().analysisMultiPv = 1;
    h.app->push(std::make_unique<SettingsScreen>(*h.app));
    h.app->pop();
    h.pump();
    h.pump(1s);
    CHECK(h.analyze()->engineMoreLines().empty());
}
