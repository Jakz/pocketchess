// SPDX-License-Identifier: GPL-3.0-or-later
#include "tools/setup/Json.h"
#include "tools/setup/Setup.h"
#include "support/Test.h"

using namespace pchess::setup;

namespace {

// Shaped like a firmware 6.8+ view.json: groups of app keys, and the app entries.
constexpr const char* kView = R"({
    "@General": ["library", "U_koreader"],
    "@Games": ["chess", "sudoku"],
    "U_koreader": {
        "path": "/mnt/ext1/applications/koreader.app",
        "title": "KOReader",
        "icon": "/mnt/ext1/applications/icons/koreader.bmp"
    },
    "version": 3
})";

} // namespace

TEST_CASE("setup json: parse and write keep order, numbers and escapes") {
    const std::string text = std::string("{\"b\": 1.50, \"a\": [true, null, \"x\\\"y") + "\\" + "u00e9\"], \"c\": {}}";
    auto j = Json::parse(text);
    REQUIRE(j.has_value());
    REQUIRE_EQ(j->members.size(), size_t(3));
    CHECK_EQ(j->members[0].first, std::string("b"));
    CHECK_EQ(j->members[0].second.text, std::string("1.50"));
    auto again = Json::parse(j->dump());
    REQUIRE(again.has_value());
    CHECK_EQ(again->dump(), j->dump());
    CHECK(j->dump().find(std::string("\\") + "u00e9") != std::string::npos);  // kept escaped
    CHECK_FALSE(Json::parse("{\"a\": }").has_value());
    CHECK_FALSE(Json::parse("{} trailing").has_value());
}

TEST_CASE("setup launcher: the PocketChess entry with its icons, a group, and back again") {
    auto plain = withPocketChess(kView, "");
    REQUIRE(plain.has_value());
    auto doc = Json::parse(*plain);
    REQUIRE(doc.has_value());
    Json* e = doc->find("U_pocketchess");
    REQUIRE(e != nullptr);
    CHECK_EQ(e->find("icon")->text, std::string("/mnt/ext1/applications/icons/pocketchess.bmp"));
    CHECK_EQ(e->find("path")->text, std::string("/mnt/ext1/applications/pocketchess.app"));
    CHECK_EQ(doc->find("version")->text, std::string("3"));
    const auto twice = withPocketChess(*plain, "");  // a second install changes nothing
    REQUIRE(twice.has_value());
    CHECK_EQ(*twice, *plain);

    auto grouped = withPocketChess(*plain, "Games");  // "@Games" matches without the @
    REQUIRE(grouped.has_value());
    auto g = Json::parse(*grouped);
    const Json& games = *g->find("@Games");
    REQUIRE_EQ(games.items.size(), size_t(3));
    CHECK_EQ(games.items.back().text, std::string("U_pocketchess"));
    // Moving to another group takes it out of the first one.
    auto moved = Json::parse(withPocketChess(*grouped, "@General").value());
    CHECK_EQ(moved->find("@Games")->items.size(), size_t(2));
    CHECK_EQ(moved->find("@General")->items.back().text, std::string("U_pocketchess"));
    CHECK_FALSE(withPocketChess(kView, "Nowhere").has_value());

    auto back = Json::parse(withoutPocketChess(*grouped));
    CHECK(back->find("U_pocketchess") == nullptr);
    CHECK_EQ(back->find("@Games")->items.size(), size_t(2));
    CHECK(back->find("U_koreader") != nullptr);
    CHECK_FALSE(withPocketChess("not json", "").has_value());
}
