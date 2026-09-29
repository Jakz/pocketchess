// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Clock.h"
#include "core/util/Files.h"
#include "core/util/Yaml.h"
#include "support/Test.h"

#include <filesystem>
#include <fstream>

using namespace pchess;
using namespace pchess::util;

TEST_CASE("yaml: block mappings, nested maps, comments and scalars") {
    auto r = parseYaml(
        "# header comment\n"
        "id: stockfish\n"
        "name: \"Stockfish 19\"   # trailing comment\n"
        "url: https://example.org/a#b\n"
        "count: 42\n"
        "neg: -7\n"
        "ratio: 0.55\n"
        "flag: true\n"
        "off: No\n"
        "nothing: ~\n"
        "empty:\n"
        "resources:\n"
        "  disk_mb: null\n"
        "  energy_class: high\n"
        "  nested:\n"
        "    deep: 'it''s'\n"
        "after: 1\n");
    if (!r) MESSAGE(r.error().message());
    REQUIRE(r.ok());
    const YamlNode& d = *r;
    CHECK(d.isMap());
    CHECK_EQ(d.size(), size_t(12));
    CHECK_EQ(d["id"].stringOr(""), std::string("stockfish"));
    CHECK_EQ(d["name"].stringOr(""), std::string("Stockfish 19"));
    CHECK(d["name"].quoted());
    CHECK_EQ(d["url"].stringOr(""), std::string("https://example.org/a#b"));
    CHECK_EQ(d["count"].intOr(0), int64_t(42));
    CHECK_EQ(d["neg"].intOr(0), int64_t(-7));
    CHECK(d["ratio"].asDouble().has_value());
    CHECK(*d["ratio"].asDouble() > 0.549);
    CHECK_FALSE(d["ratio"].asInt().has_value());
    CHECK(d["flag"].boolOr(false));
    CHECK_FALSE(d["off"].boolOr(true));
    CHECK(d["nothing"].isNull());
    CHECK(d["empty"].isNull());
    CHECK(d.has("empty"));
    CHECK_FALSE(d.has("missing"));
    CHECK(d["missing"]["deeper"].isNull());
    CHECK(d["resources"]["disk_mb"].isNull());
    CHECK_EQ(d["resources"]["energy_class"].stringOr(""), std::string("high"));
    CHECK_EQ(d["resources"]["nested"]["deep"].stringOr(""), std::string("it's"));
    CHECK_EQ(d["after"].intOr(0), int64_t(1));
    CHECK_EQ(d["resources"].line(), 13);  // a block value starts on its first entry
    // Order is kept.
    CHECK_EQ(d.entries().front().first, std::string("id"));
    CHECK_EQ(d.entries().back().first, std::string("after"));
}

TEST_CASE("yaml: flow collections, quoted keys, multi-line flow") {
    auto r = parseYaml(
        "binary: { arm: stockfish, windows-x64: stockfish.exe }\n"
        "roles: [play, analysis, hint, validate]\n"
        "files: []\n"
        "strength:\n"
        "  beginner: { options: { \"Skill Level\": 0 },  limits: { movetime: 80 } }\n"
        "  maximum:  { options: {} }\n"
        "multi: [a,\n"
        "        'b, c',\n"
        "        {x: 1}]\n"
        "esc: \"tab\\there \\u00e9 \\\"q\\\"\"\n");
    if (!r) MESSAGE(r.error().message());
    REQUIRE(r.ok());
    const YamlNode& d = *r;
    CHECK_EQ(d["binary"]["arm"].stringOr(""), std::string("stockfish"));
    CHECK_EQ(d["binary"]["windows-x64"].stringOr(""), std::string("stockfish.exe"));
    REQUIRE(d["roles"].isSequence());
    CHECK_EQ(d["roles"].size(), size_t(4));
    CHECK_EQ(d["roles"][3].stringOr(""), std::string("validate"));
    CHECK_EQ(d["roles"].stringList().size(), size_t(4));
    CHECK(d["files"].isSequence());
    CHECK_EQ(d["files"].size(), size_t(0));
    CHECK_EQ(d["strength"]["beginner"]["options"]["Skill Level"].intOr(-1), int64_t(0));
    CHECK_EQ(d["strength"]["beginner"]["limits"]["movetime"].intOr(-1), int64_t(80));
    CHECK(d["strength"]["maximum"]["options"].isMap());
    CHECK_EQ(d["strength"]["maximum"]["options"].size(), size_t(0));
    REQUIRE(d["multi"].isSequence());
    CHECK_EQ(d["multi"].size(), size_t(3));
    CHECK_EQ(d["multi"][1].stringOr(""), std::string("b, c"));
    CHECK_EQ(d["multi"][2]["x"].intOr(0), int64_t(1));
    CHECK_EQ(d["esc"].stringOr(""), std::string("tab\there \xC3\xA9 \"q\""));
}

TEST_CASE("yaml: block sequences, '- key: value' items and nested sequences") {
    auto r = parseYaml(
        "---\n"
        "list:\n"
        "  - one\n"
        "  - 2\n"
        "  -\n"
        "    inner: yes\n"
        "sameIndent:\n"
        "- a\n"
        "- b\n"
        "items:\n"
        "  - name: first\n"
        "    value: 1\n"
        "  - name: second\n"
        "    tags: [x]\n"
        "  - - n1\n"
        "    - n2\n"
        "tail: end\n");
    if (!r) MESSAGE(r.error().message());
    REQUIRE(r.ok());
    const YamlNode& d = *r;
    REQUIRE(d["list"].isSequence());
    CHECK_EQ(d["list"].size(), size_t(3));
    CHECK_EQ(d["list"][0].stringOr(""), std::string("one"));
    CHECK_EQ(d["list"][1].intOr(0), int64_t(2));
    CHECK(d["list"][2]["inner"].boolOr(false));
    CHECK_EQ(d["sameIndent"].size(), size_t(2));
    REQUIRE(d["items"].isSequence());
    CHECK_EQ(d["items"].size(), size_t(3));
    CHECK_EQ(d["items"][0]["name"].stringOr(""), std::string("first"));
    CHECK_EQ(d["items"][0]["value"].intOr(0), int64_t(1));
    CHECK_EQ(d["items"][1]["tags"][0].stringOr(""), std::string("x"));
    CHECK(d["items"][2].isSequence());
    CHECK_EQ(d["items"][2][1].stringOr(""), std::string("n2"));
    CHECK_EQ(d["tail"].stringOr(""), std::string("end"));

    auto top = parseYaml("- a\n- b: 1\n");
    REQUIRE(top.ok());
    CHECK(top->isSequence());
    CHECK_EQ((*top)[1]["b"].intOr(0), int64_t(1));

    auto empty = parseYaml("# only a comment\n\n");
    REQUIRE(empty.ok());
    CHECK(empty->isNull());
    auto crlf = parseYaml("a: 1\r\nb: two\r\n");
    REQUIRE(crlf.ok());
    CHECK_EQ((*crlf)["b"].stringOr(""), std::string("two"));
}

TEST_CASE("yaml: malformed or unsupported input is an error with a line number") {
    auto expectError = [](const char* text, const char* fragment) {
        auto r = parseYaml(text);
        CHECK_FALSE(r.ok());
        if (!r.ok()) {
            const bool has = r.error().context.find(fragment) != std::string::npos;
            if (!has) MESSAGE(r.error().context);
            CHECK(has);
        }
    };
    expectError("a: 1\na: 2\n", "line 2: duplicate key");
    expectError("a: 1\n\tb: 2\n", "line 2: tab");
    expectError("a: [1, 2\n", "line 1");
    expectError("a: {x: 1]}\n", "line 1");
    expectError("a: &anchor 1\n", "unsupported");
    expectError("a: |\n  text\n", "unsupported");
    expectError("a: \"open\n", "unterminated");
    expectError("a:\n    b: 1\n  c: 2\n", "line 3: bad indentation");
    expectError("a: 1\n  b: 2\n", "line 2: unexpected indentation");
    expectError("just text\nmore: 1\n", "line 2");
    expectError("a: 1\n---\nb: 2\n", "multiple documents");
}

TEST_CASE("clock: civil time from unix seconds (UTC)") {
    auto eq = [](const util::CivilTime& c, int y, int mo, int d, int h, int mi, int s) {
        return c.year == y && c.month == mo && c.day == d && c.hour == h && c.minute == mi && c.second == s;
    };
    CHECK(eq(util::civilFromUnix(0), 1970, 1, 1, 0, 0, 0));
    CHECK(eq(util::civilFromUnix(951782400), 2000, 2, 29, 0, 0, 0));        // leap day
    CHECK(eq(util::civilFromUnix(1790000000), 2026, 9, 21, 14, 13, 20));
    CHECK(eq(util::civilFromUnix(4102444799), 2099, 12, 31, 23, 59, 59));
    CHECK(eq(util::civilFromUnix(-1), 1969, 12, 31, 23, 59, 59));
}

TEST_CASE("files: readTextFile reads bytes as they are, reports missing and oversized files") {
    const auto dir = std::filesystem::temp_directory_path() / "pchess-files-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::string bytes = std::string("line 1\r\nline 2\n\0tail", 20);
    {
        std::ofstream(dir / "a.pgn", std::ios::binary) << bytes;
    }
    auto r = readTextFile(dir / "a.pgn");
    REQUIRE(r.ok());
    CHECK(*r == bytes);  // CRLF and NUL kept
    auto big = readTextFile(dir / "a.pgn", 10);
    REQUIRE_FALSE(big.ok());
    CHECK(big.error().code == ErrorCode::Unsupported);
    auto missing = readTextFile(dir / "none.pgn");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
    std::filesystem::remove_all(dir, ec);
}
