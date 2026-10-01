// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "app/SettingsScreen.h"
#include "support/Golden.h"
#include "support/Test.h"
#include "ui/theme/Palette.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;

namespace {

struct SettingsHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    std::unique_ptr<Application> app;

    explicit SettingsHarness(const std::filesystem::path& root = {}) {
        AppConfig cfg;
        cfg.root = root.empty() ? std::filesystem::temp_directory_path() / "pocketchess-tests" : root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = !root.empty();  // only the persistence test uses its own root
        cfg.startScreen = "settings";
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    SettingsScreen* screen() const { return dynamic_cast<SettingsScreen*>(app->top()); }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
    }
    // Opens a row as a tap would (toggle, choice dialog or swatches).
    void open(const std::string& label) {
        REQUIRE(screen() != nullptr);
        REQUIRE(screen()->open(label));
    }
};

} // namespace

TEST_CASE("settings: key/value round trip; unknown keys and invalid values keep the defaults") {
    Settings s;
    s.mono = true;
    s.boardTheme = "Slate";
    s.fullRefreshEvery = 30;
    s.powerProfile = engine::PowerProfileId::Eco;
    s.analysisEngine = "stockfish11";
    s.deepLimitSeconds = -1;
    s.analysisMultiPv = 3;
    s.evalFromWhite = false;
    s.pieceSet = "Merida";
    s.swapPageKeys = true;
    s.debugLogging = true;
    Settings back;
    back.fromMap(s.toMap());
    CHECK(back.toMap() == s.toMap());

    Settings d;
    d.fromMap({{"display.mono", "maybe"},
               {"display.full_refresh_every", "0"},
               {"engine.multipv", "9"},
               {"engine.threads", "2x"},
               {"engine.power_profile", "turbo"},
               {"future.option", "1"}});
    CHECK(d.toMap() == Settings().toMap());
    d.fromMap({{"engine.power_profile", "eco"}, {"engine.deep_limit_s", "60"}});
    CHECK(d.powerProfile == engine::PowerProfileId::Eco);
    CHECK_EQ(d.deepLimitSeconds, 60);
}

TEST_CASE("settings screen: pages, toggles, choice dialog, applied to the engine manager") {
    SettingsHarness h;
    REQUIRE(h.screen() != nullptr);
    CHECK(h.screen()->pageCount() >= 2);
    CHECK_FALSE(h.app->settingsPersisted());
    test::checkGolden("settings", h.app->canvas());

    // Two-choice rows toggle in place.
    h.open("Coordinates");
    CHECK_FALSE(h.app->settings().coordinates);
    CHECK_FALSE(h.app->theme().coordinates);  // applied at once
    h.open("Coordinates");
    CHECK(h.app->settings().coordinates);

    // Multi-choice rows open a dialog; the engine manager sees the change for its next job.
    h.open("Power profile");
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("message"));
    h.run("key:next;key:next;key:ok");  // Performance, Balanced, [Eco]
    REQUIRE(h.screen() != nullptr);
    CHECK(h.app->settings().powerProfile == engine::PowerProfileId::Eco);
    CHECK(h.app->engines().settings().profile == engine::PowerProfileId::Eco);

    h.open("Deep analysis stops");
    h.run("key:next;key:next;key:ok");  // Profile default, After 10 s, [After 30 s]
    CHECK_EQ(h.app->settings().deepLimitSeconds, 30);
    REQUIRE(h.app->engines().settings().analysisLimit.has_value());
    CHECK_EQ(h.app->engines().settings().analysisLimit->movetimeMs.value_or(0), 30000);

    // Back from the dialog changes nothing.
    h.open("Analysis lines");
    h.run("key:back");
    CHECK_EQ(h.app->settings().analysisMultiPv, 1);

    h.open("Hash");  // the last row is on a later page
    h.run("key:back");  // closes its dialog
    CHECK(h.screen()->page() > 0);
    h.run("key:back");
    CHECK_EQ(std::string(h.app->top()->name()), std::string("home"));
}

TEST_CASE("settings screen: changes are saved in user/chess.db and survive a restart") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "pchess-settings-persist";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    {
        SettingsHarness h(root);
        REQUIRE(h.app->settingsPersisted());
        h.open("Mode");  // Color -> Monochrome
        CHECK(h.app->settings().mono);
        h.open("Evaluation");  // White's view -> Side to move
    }
    CHECK(std::filesystem::exists(root / "user" / "chess.db"));
    {
        SettingsHarness h(root);
        CHECK(h.app->settings().mono);
        CHECK(h.app->theme().mono);
        CHECK_FALSE(h.app->settings().evalFromWhite);
    }
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("settings screen: keys only - Prev/Next turn pages, Menu changes a setting or goes back") {
    SettingsHarness h;
    REQUIRE(h.screen() != nullptr);
    h.run("key:next");
    CHECK_EQ(h.screen()->page(), 1);
    h.run("key:prev");
    CHECK_EQ(h.screen()->page(), 0);
    h.run("key:menu;key:ok");  // the menu lists this page's settings: Mode, Color -> Monochrome
    CHECK(h.app->settings().mono);
    h.app->settings().mono = false;
    h.app->applySettings();
    // "← Back to Home" is the last entry, just before Close, on the menu's last page.
    h.run("key:menu;key:prev;key:prev;key:ok");  // Close, More… -> the last page
    h.run("key:prev;key:prev;key:ok");          // Close, ← Back to Home
    CHECK(h.screen() == nullptr);
    CHECK_EQ(std::string(h.app->top()->name()), std::string("home"));
}

TEST_CASE("settings screen: accent and piece colors are picked from swatches and color the board") {
    SettingsHarness h;
    h.open("Accent color");
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("swatches"));
    for (int i = 0; i < 7; ++i) h.run("key:next");  // Theme, Graphite, Red, Orange, Amber, Green, Teal -> Blue
    h.app->flush();
    test::checkGolden("swatches", h.app->canvas());
    h.run("key:ok");
    CHECK_EQ(h.app->settings().accent, std::string("Blue"));
    CHECK((h.app->theme().accent == *ui::findAccent("Blue")));

    h.open("Piece colors");
    for (int i = 0; i < 7; ++i) h.run("key:next");
    h.run("key:ok");
    CHECK_EQ(h.app->settings().pieceColor, std::string("Blue"));
    CHECK(h.app->theme().tintPieces);

    // Mono mode ignores both (patterns and grays only).
    h.app->settings().mono = true;
    h.app->applySettings();
    CHECK_FALSE(h.app->theme().tintPieces);
    h.app->settings().mono = false;
    h.app->applySettings();

    h.app->pop();
    h.app->push(std::make_unique<AnalyzeScreen>(*h.app, std::string()));
    REQUIRE(runScript(*h.app, "tap:604,934").ok());  // select e2: accent-tinted square
    h.app->flush();
    test::checkGolden("analyze-colors", h.app->canvas());
    h.app->settings().accent.clear();
    h.app->settings().pieceColor.clear();
    h.app->applySettings();
}
