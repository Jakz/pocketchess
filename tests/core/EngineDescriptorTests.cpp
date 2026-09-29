// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/EngineDescriptor.h"
#include "core/engine/EngineRegistry.h"
#include "core/engine/EngineTypes.h"
#include "core/util/Yaml.h"
#include "support/Test.h"

#include <filesystem>
#include <fstream>

using namespace pchess;
using namespace pchess::engine;

namespace {
SearchLimits movetime(int ms) {
    SearchLimits l;
    l.movetimeMs = ms;
    return l;
}
} // namespace

TEST_CASE("engine types: limits, option sets and validation against capabilities") {
    CHECK_EQ(movetime(500).goCommand(), std::string("go movetime 500"));
    SearchLimits d;
    d.depth = 12;
    d.nodes = 5000;
    CHECK_EQ(d.goCommand(), std::string("go depth 12 nodes 5000"));
    CHECK_EQ(SearchLimits{}.goCommand(), std::string("go infinite"));
    CHECK_EQ(movetime(100).overriddenBy(d).goCommand(), std::string("go depth 12 nodes 5000 movetime 100"));

    OptionSet set{{"Hash", "16"}};
    setOption(set, "hash", "32");
    setOption(set, "Threads", "2");
    CHECK_EQ(set.size(), size_t(2));
    CHECK_EQ(*findOption(set, "HASH"), std::string("32"));
    CHECK(findOption(set, "MultiPV") == nullptr);

    Capabilities caps;
    caps.name = "X";
    for (const char* l : {"option name Hash type spin default 16 min 1 max 256",
                          "option name UCI_LimitStrength type check default false",
                          "option name Style type combo default Normal var Solid var Normal var Risky",
                          "option name Clear Hash type button"})
        caps.options.push_back(parseUciLine(l).option);
    std::vector<std::string> warnings;
    const OptionSet v = validateOptions({{"hash", "4096"},
                                         {"uci_limitstrength", "yes"},
                                         {"Style", "risky"},
                                         {"Clear Hash", ""},
                                         {"Skill Level", "3"},
                                         {"Style", "wild"}},
                                        caps, &warnings);
    REQUIRE_EQ(v.size(), size_t(4));
    CHECK_EQ(v[0].first, std::string("Hash"));
    CHECK_EQ(v[0].second, std::string("256"));
    CHECK_EQ(v[1].second, std::string("true"));
    CHECK_EQ(v[2].second, std::string("Risky"));
    CHECK_EQ(v[3].first, std::string("Clear Hash"));
    CHECK_EQ(warnings.size(), size_t(3));  // clamp, unknown option, bad combo value
}

TEST_CASE("engine descriptor: the shipped stockfish descriptor parses") {
    // data/fonts -> data/engines (desktop); <root>/assets/fonts -> <root>/engines (device self-test).
    const std::filesystem::path fonts(test::fontDir());
    std::filesystem::path file = fonts.parent_path() / "engines" / "stockfish" / "engine.yaml";
    if (!std::filesystem::exists(file))
        file = fonts.parent_path().parent_path() / "engines" / "stockfish" / "engine.yaml";
    auto d = loadDescriptor(file);
    if (!d) MESSAGE(d.error().message());
    REQUIRE(d.ok());
    CHECK_EQ(d->id, std::string("stockfish"));
    CHECK_EQ(d->name, std::string("Stockfish 19"));
    CHECK_EQ(d->binaries.at("arm"), std::string("stockfish"));
    CHECK_EQ(d->binaries.at("windows-x64"), std::string("stockfish.exe"));
    CHECK_EQ(d->roles.size(), size_t(4));
    CHECK(d->resources.energy == EnergyClass::High);
    CHECK_FALSE(d->resources.rssMb.has_value());
    CHECK_EQ(*findOption(d->defaults, "Hash"), std::string("16"));
    REQUIRE_EQ(d->strength.size(), size_t(5));
    const StrengthLevel* b = d->strengthFor("beginner");
    REQUIRE(b != nullptr);
    CHECK_EQ(*findOption(b->options, "Skill Level"), std::string("0"));
    CHECK_EQ(b->limits.movetimeMs.value_or(0), 80);
    const StrengthLevel* club = d->strengthFor("club");
    REQUIRE(club != nullptr);
    CHECK_EQ(*findOption(club->options, "UCI_LimitStrength"), std::string("true"));
    CHECK_EQ(*findOption(club->options, "UCI_Elo"), std::string("1600"));
    CHECK(d->strengthFor("maximum")->options.empty());
    CHECK_EQ(d->binaryPath("arm"), file.parent_path() / "stockfish");
    CHECK(d->binaryPath("amiga").empty());
}

TEST_CASE("engine descriptor: errors name the file and line; registry disables broken engines") {
    auto bad = [](const char* yaml) {
        auto doc = util::parseYaml(yaml);
        REQUIRE(doc.ok());
        return parseDescriptor(*doc, "engines/x");
    };
    CHECK_FALSE(bad("name: no id\nbinary: x\n").ok());
    CHECK_FALSE(bad("id: x\n").ok());  // no binary
    CHECK_FALSE(bad("id: 'bad id'\nbinary: x\n").ok());
    CHECK_FALSE(bad("id: x\nbinary: x\nprotocol: xboard\n").ok());
    CHECK_FALSE(bad("id: x\nbinary: x\nroles: [play, dance]\n").ok());
    auto lim = bad("id: x\nbinary: x\nstrength:\n  casual: { limits: { movetime: fast } }\n");
    REQUIRE_FALSE(lim.ok());
    CHECK(lim.error().context.find("line 4") != std::string::npos);
    auto ok = bad("id: tiny\nbinary: tiny.exe\nresources: { energy_class: low }\n");
    REQUIRE(ok.ok());
    CHECK_EQ(ok->roles.size(), size_t(4));  // all roles by default
    CHECK(ok->resources.energy == EnergyClass::Low);

    // A temporary engines/ folder: one good engine, one without its binary, one malformed.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "pchess-registry-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "good");
    std::filesystem::create_directories(root / "nobinary");
    std::filesystem::create_directories(root / "broken");
    std::filesystem::create_directories(root / "light");
    const std::string plat = currentPlatformKey();
    std::ofstream(root / "good" / "engine.yaml") << "id: good\nbinary: { " << plat << ": good.bin }\n";
    std::ofstream(root / "good" / "good.bin") << "x";
    std::ofstream(root / "light" / "engine.yaml")
        << "id: light\nbinary: { " << plat << ": l.bin }\nroles: [play, hint]\nresources: { energy_class: low }\n";
    std::ofstream(root / "light" / "l.bin") << "x";
    std::ofstream(root / "nobinary" / "engine.yaml") << "id: nobinary\nbinary: { " << plat << ": missing.bin }\n";
    std::ofstream(root / "broken" / "engine.yaml") << "id: [oops\n";

    EngineRegistry reg;
    reg.scan(root);
    REQUIRE_EQ(reg.entries().size(), size_t(4));
    CHECK_EQ(reg.entries()[0].descriptor.id, std::string("broken"));  // sorted by id
    CHECK_FALSE(reg.find("broken")->available);
    CHECK(reg.find("broken")->problem.find("line 1") != std::string::npos);
    CHECK(reg.available("good") != nullptr);
    CHECK(reg.available("nobinary") == nullptr);
    CHECK(reg.find("nobinary")->problem.find("binary not found") != std::string::npos);
    CHECK_EQ(reg.availableFor(Role::Analysis).size(), size_t(1));
    CHECK_EQ(reg.availableFor(Role::Hint).size(), size_t(2));
    CHECK_EQ(reg.lightestFor(Role::Hint)->id, std::string("light"));
    CHECK_EQ(reg.lightestFor(Role::Analysis)->id, std::string("good"));

    reg.scan(root / "does-not-exist");
    CHECK(reg.entries().empty());
    std::filesystem::remove_all(root);
}
