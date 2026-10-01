// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/OpeningsScreen.h"
#include "app/Script.h"
#include "core/games/Openings.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;

namespace {

namespace fs = std::filesystem;

// A root whose assets/openings holds the bundled book (data/openings on the desktop).
fs::path openingsRoot() {
    const fs::path root = fs::temp_directory_path() / "pchess-openings";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "assets" / "openings");
    const fs::path src = fs::path(test::fontDir()).parent_path() / "openings";
    for (const char* f : {"a.tsv", "b.tsv", "c.tsv", "d.tsv", "e.tsv"}) fs::copy_file(src / f, root / "assets" / "openings" / f);
    return root;
}

struct OpeningsHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    std::unique_ptr<Application> app;
    explicit OpeningsHarness(const fs::path& root) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = false;
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    OpeningsScreen* screen() const { return dynamic_cast<OpeningsScreen*>(app->top()); }
};

} // namespace

TEST_CASE("openings: families, their variations, search, and a line opened in Analyze") {
    const fs::path root = openingsRoot();
    OpeningsHarness h(root);
    REQUIRE(h.app->openings() != nullptr);
    CHECK(h.app->openings()->lines().size() > 3000);
    h.app->push(std::make_unique<OpeningsScreen>(*h.app));
    REQUIRE(h.screen() != nullptr);
    CHECK(h.screen()->items().size() > 100);  // families
    h.app->flush();
    test::checkGolden("openings", h.app->canvas());

    h.screen()->showFamily("Sicilian Defense");
    CHECK(h.screen()->items().size() > 50);
    CHECK(h.screen()->items().front().line != nullptr);

    h.screen()->search("najdorf");
    REQUIRE_FALSE(h.screen()->items().empty());
    for (const auto& it : h.screen()->items()) CHECK(it.title.find("Najdorf") != std::string::npos);
    const size_t plies = h.screen()->items().front().line->moves.size();
    h.screen()->open(0);
    auto* analyze = dynamic_cast<AnalyzeScreen*>(h.app->top());
    REQUIRE(analyze != nullptr);
    CHECK_EQ(analyze->tree().mainLine().size(), plies);

    // Keys only: Menu -> "Open a line…" -> the first one.
    h.app->pop();
    REQUIRE(runScript(*h.app, "key:menu;key:ok;key:ok").ok());
    CHECK(dynamic_cast<AnalyzeScreen*>(h.app->top()) != nullptr);
    std::error_code ec;
    h.app.reset();
    fs::remove_all(root, ec);
}
