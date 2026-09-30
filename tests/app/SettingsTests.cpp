// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "app/SettingsScreen.h"
#include "support/Golden.h"
#include "support/Test.h"

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
    // Moves the focus (Next key, across pages) until `label` is focused.
    void focus(const std::string& label) {
        for (int i = 0; i < 40 && screen() && screen()->focusedLabel() != label; ++i) run("key:next");
        REQUIRE(screen() != nullptr);
        REQUIRE_EQ(screen()->focusedLabel(), label);
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
    h.focus("Coordinates");
    h.run("key:ok");
    CHECK_FALSE(h.app->settings().coordinates);
    CHECK_FALSE(h.app->theme().coordinates);  // applied at once
    h.run("key:ok");
    CHECK(h.app->settings().coordinates);

    // Multi-choice rows open a dialog; the engine manager sees the change for its next job.
    h.focus("Power profile");
    h.run("key:ok");
    REQUIRE_EQ(std::string(h.app->top()->name()), std::string("message"));
    h.run("key:next;key:next;key:ok");  // Performance, Balanced, [Eco]
    REQUIRE(h.screen() != nullptr);
    CHECK(h.app->settings().powerProfile == engine::PowerProfileId::Eco);
    CHECK(h.app->engines().settings().profile == engine::PowerProfileId::Eco);

    h.focus("Deep analysis stops");
    h.run("key:ok;key:next;key:next;key:ok");  // Profile default, After 10 s, [After 30 s]
    CHECK_EQ(h.app->settings().deepLimitSeconds, 30);
    REQUIRE(h.app->engines().settings().analysisLimit.has_value());
    CHECK_EQ(h.app->engines().settings().analysisLimit->movetimeMs.value_or(0), 30000);

    // Back from the dialog changes nothing.
    h.focus("Analysis lines");
    h.run("key:ok;key:back");
    CHECK_EQ(h.app->settings().analysisMultiPv, 1);

    h.focus("Hash");  // the last row is on a later page
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
        h.focus("Mode");
        h.run("key:ok");  // Color -> Monochrome
        CHECK(h.app->settings().mono);
        h.focus("Evaluation");
        h.run("key:ok");  // White's view -> Side to move
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

TEST_CASE("settings screen: keys only - Prev above the first row focuses Back, Menu leaves") {
    SettingsHarness h;
    REQUIRE(h.screen() != nullptr);
    h.run("key:next");  // first row
    CHECK_EQ(h.screen()->focusedLabel(), std::string("Mode"));
    h.run("key:prev");  // "‹ Back"
    CHECK(h.screen()->focusedLabel().empty());
    h.run("key:next");
    CHECK_EQ(h.screen()->focusedLabel(), std::string("Mode"));
    h.run("key:prev;key:menu");
    CHECK(h.screen() == nullptr);  // back on Home
    CHECK_EQ(std::string(h.app->top()->name()), std::string("home"));
}
