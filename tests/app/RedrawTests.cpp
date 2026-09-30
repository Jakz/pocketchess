// SPDX-License-Identifier: GPL-3.0-or-later
// Partial redraws must produce exactly what a full redraw produces. A screen whose areas overlap
// (text over a button row, say) repaints one area over another on a partial update, and the
// reader then shows a half-drawn control until the next full refresh.
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/GamesScreen.h"
#include "app/HeadlessHost.h"
#include "app/ImportScreen.h"
#include "app/Script.h"
#include "app/SettingsScreen.h"
#include "core/chess/Pgn.h"
#include "core/games/GamePack.h"
#include "core/util/Files.h"
#include "support/FakeEngine.h"
#include "support/Test.h"
#include "ui/canvas/PngWriter.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;

namespace {

namespace fs = std::filesystem;

struct RedrawHarness {
    HeadlessHost host;
    HeadlessPresenter presenter;
    test::FakeEngineFactory fake;
    std::unique_ptr<Application> app;

    explicit RedrawHarness(const fs::path& root) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = false;
        cfg.extraEngines.push_back(test::fakeDescriptor("fakefish"));
        cfg.engineFactory = [this](const engine::EngineDescriptor& d, util::Scheduler& s) {
            return std::make_unique<engine::UciEngine>(d, s, test::selfExe(), fake.factory());
        };
        fake.behaviour.extraOptions = {"option name MultiPV type spin default 1 min 1 max 500"};
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    void pump(std::chrono::milliseconds ms = 0ms) {
        host.advance(ms);
        app->flush();
    }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
        pump();
    }

    // The frame after partial updates vs the same frame fully redrawn.
    void check(const std::string& what) {
        app->flush();
        const ui::Canvas& c = app->canvas();
        const ui::Canvas before = c;
        const std::vector<uint8_t> partial(c.data(), c.data() + static_cast<size_t>(c.stride()) * c.height());
        app->invalidateAll(ui::RefreshHint::Content);
        app->flush();
        int top = -1, bottom = -1;
        for (int y = 0; y < c.height(); ++y)
            if (!std::equal(c.row(y), c.row(y) + c.stride(), partial.data() + static_cast<size_t>(y) * c.stride())) {
                if (top < 0) top = y;
                bottom = y;
            }
        if (top >= 0) {
            MESSAGE(what + ": partial redraw differs from a full redraw in rows " + std::to_string(top) + ".." +
                    std::to_string(bottom) + " (full redraw saved to actual/redraw-" + what + ".png)");
            const fs::path dir = fs::path(test::goldenDir()) / "actual";
            std::error_code ec;
            fs::create_directories(dir, ec);
            (void)ui::writePng(dir / ("redraw-" + what + ".png"), c);
            (void)ui::writePng(dir / ("redraw-" + what + "-partial.png"), before);
        }
        if (top >= 0) {
            int left = c.width(), right = -1;
            for (int y = top; y <= bottom; ++y)
                for (int x = 0; x < c.width(); ++x)
                    if (std::memcmp(c.row(y) + x * 3, partial.data() + static_cast<size_t>(y) * c.stride() + x * 3, 3) != 0) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                    }
            MESSAGE("  columns " + std::to_string(left) + ".." + std::to_string(right));
        }
        CHECK(top < 0);
    }
};

fs::path redrawRoot() {
    const fs::path root = fs::temp_directory_path() / "pchess-redraw";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path pack = root / "content" / "games" / "sample.db";
    fs::create_directories(pack.parent_path());
    auto db = storage::Database::open(pack, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    games::GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    fs::path pgn = fs::path(test::fixtureDir()) / "classics.pgn";
    if (!fs::exists(pgn)) pgn = fs::path(test::fontDir()).parent_path() / "samples" / "pgn" / "classics.pgn";
    auto text = util::readTextFile(pgn);
    REQUIRE(text.ok());
    for (auto& g : chess::parsePgn(*text)) REQUIRE(imp.add(*g).ok());
    REQUIRE(imp.finish({{"title", "Classics"}}).ok());
    return root;
}

} // namespace

TEST_CASE("redraw: Analyze with three engine lines keeps the button row intact") {
    const fs::path root = redrawRoot();
    RedrawHarness h(root);
    h.app->push(std::make_unique<AnalyzeScreen>(*h.app, std::string()));
    h.pump();
    h.check("analyze-start");
    for (int lines : {1, 2, 3}) {
        h.app->settings().analysisMultiPv = lines;
        auto* a = dynamic_cast<AnalyzeScreen*>(h.app->top());
        REQUIRE(a != nullptr);
        a->setEngineMode(AnalyzeScreen::EngineMode::Off);
        a->setEngineMode(AnalyzeScreen::EngineMode::Quick);
        h.pump();
        h.pump(1s);
        h.check("analyze-engine-" + std::to_string(lines));
        h.run("tap:604,934;tap:604,666");  // e2-e4: new engine lines, move list
        h.pump(1s);
        h.check("analyze-after-move-" + std::to_string(lines));
    }
}

TEST_CASE("redraw: menus, dialogs and list screens redraw cleanly") {
    const fs::path root = redrawRoot();
    RedrawHarness h(root);
    h.check("home");
    h.run("key:next;key:next");
    h.check("home-focus");

    h.app->push(std::make_unique<GamesScreen>(*h.app));
    h.pump();
    h.check("games");
    h.run("key:menu");
    h.check("games-menu");
    h.run("key:ok;key:ok");  // Open a game… -> the first game
    h.check("study");
    for (int i = 0; i < 6; ++i) h.run("key:next");
    h.check("study-steps");
    h.run("key:menu");
    h.check("study-menu");
    h.run("key:prev;key:prev;key:ok");  // Back to the list
    h.check("games-back");
    h.app->pop();
    h.pump();

    h.app->push(std::make_unique<SettingsScreen>(*h.app));
    h.pump();
    for (int i = 0; i < 3; ++i) h.run("key:next");
    h.check("settings-focus");
    h.run("key:ok");  // a choice dialog
    h.check("settings-dialog");
    h.run("key:menu");
    h.check("settings-after-dialog");
    h.app->pop();
    h.pump();

    h.app->push(std::make_unique<ImportScreen>(*h.app));
    h.pump();
    h.check("import-empty");
}
