// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/MoveCodec.h"
#include "core/chess/Pgn.h"
#include "core/chess/San.h"
#include "core/games/GamePack.h"
#include "core/games/Openings.h"
#include "core/util/Files.h"
#include "support/Test.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace pchess;
using namespace pchess::games;

namespace {

// data/openings next to data/fonts (desktop only: the device does not ship the TSVs).
std::optional<OpeningBook> openingBook() {
    const auto dir = std::filesystem::path(test::fontDir()).parent_path() / "openings";
    if (!std::filesystem::exists(dir / "a.tsv")) return std::nullopt;
    OpeningBook book;
    for (const char* f : {"a.tsv", "b.tsv", "c.tsv", "d.tsv", "e.tsv"}) {
        auto text = util::readTextFile(dir / f);
        REQUIRE(text.ok());
        REQUIRE(book.addTsv(*text).ok());
    }
    return book;
}

// tests/fixtures, else data/samples/pgn (desktop; the device stages both into selftest/fixtures).
std::string repoText(const std::filesystem::path& rel) {
    std::filesystem::path file = std::filesystem::path(test::fixtureDir()) / rel;
    if (!std::filesystem::exists(file))
        file = std::filesystem::path(test::fontDir()).parent_path() / "samples" / "pgn" / rel;
    auto t = util::readTextFile(file);
    if (!t) MESSAGE(t.error().message());
    REQUIRE(t.ok());
    return *t;
}

std::vector<chess::Move> sanLine(std::initializer_list<const char*> sans) {
    std::vector<chess::Move> out;
    chess::Position pos = chess::Position::startPosition();
    for (const char* s : sans) {
        out.push_back(chess::parseSan(pos, s).value());
        pos.makeMove(out.back());
    }
    return out;
}

// The fixture (39 Lichess games, CC0) plus three annotated classics, imported into memory.
GamePack samplePack(ImportStats* stats = nullptr) {
    auto db = storage::Database::open({}, storage::Database::Mode::Memory);
    REQUIRE(db.ok());
    GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    for (const char* file : {"games-sample.pgn", "classics.pgn"})
        for (auto& g : chess::parsePgn(repoText(file))) {
            REQUIRE(g.ok());
            REQUIRE(imp.add(*g).ok());
        }
    REQUIRE(imp.finish({{"source", "test"}}).ok());
    if (stats) *stats = imp.stats();
    auto pack = GamePack::fromDatabase(std::move(*db), "sample");
    REQUIRE(pack.ok());
    return std::move(*pack);
}

} // namespace

TEST_CASE("games: player names are normalized for search") {
    CHECK_EQ(normalizePlayer("Carlsen, Magnus"), std::string("carlsen magnus"));
    CHECK_EQ(normalizePlayer("  Duke Karl / Count  Isouard "), std::string("duke karl count isouard"));
    CHECK_EQ(normalizePlayer("DESMOND_wilson"), std::string("desmond wilson"));
    CHECK_EQ(normalizePlayer("Gašpar"), std::string("gašpar"));  // non-ASCII bytes kept
}

TEST_CASE("openings: the deepest named position wins, transpositions included") {
    auto book = openingBook();
    if (!book) {
        MESSAGE("data/openings not available (device): skipped");
        return;
    }
    CHECK(book->size() > 3000);
    CHECK_EQ(book->rejected(), 0);
    const auto start = chess::Position::startPosition();
    auto najdorf = book->classify(start, sanLine({"e4", "c5", "Nf3", "d6", "d4", "cxd4", "Nxd4", "Nf6", "Nc3", "a6",
                                                  "Be2", "e5"}));
    REQUIRE(najdorf.has_value());
    CHECK_EQ(najdorf->eco.substr(0, 2), std::string("B9"));
    CHECK(najdorf->name.find("Najdorf") != std::string::npos);
    auto nimzo = book->classify(start, sanLine({"d4", "Nf6", "c4", "e6", "Nc3", "Bb4"}));
    auto transposed = book->classify(start, sanLine({"c4", "e6", "Nc3", "Nf6", "d4", "Bb4"}));
    REQUIRE(nimzo.has_value());
    REQUIRE(transposed.has_value());
    CHECK_EQ(nimzo->eco, std::string("E20"));
    CHECK_EQ(transposed->eco, nimzo->eco);
    CHECK_FALSE(book->classify(start, {}).has_value());
}

TEST_CASE("game pack: import keeps moves, and comments/variations for annotated games") {
    ImportStats st;
    GamePack pack = samplePack(&st);
    CHECK_EQ(pack.count(), 42);
    CHECK_EQ(st.games, uint64_t(42));
    CHECK_EQ(st.annotated, uint64_t(3));  // only the classics have comments

    // Round trip PGN -> DB -> GameTree: the main lines are identical.
    const auto games = chess::parsePgn(repoText("games-sample.pgn"));
    auto rows = pack.page({}, 0, 100);
    REQUIRE(rows.ok());
    int checked = 0;
    for (const auto& g : games) {
        // Lichess sites are URLs, which packs do not keep: find the game by players and moves.
        for (const auto& r : *rows) {
            if (r.white != g->header("White") || r.black != g->header("Black")) continue;
            auto back = pack.load(r.id);
            REQUIRE(back.ok());
            if (back->tree.mainLine() != g->tree.mainLine()) continue;
            CHECK_EQ(back->result, g->result);
            CHECK(r.site.empty());
            ++checked;
            break;
        }
    }
    CHECK_EQ(checked, 39);

    GameFilter immortal;
    immortal.player = "kieseritzky";
    auto im = pack.page(immortal, 0, 5);
    REQUIRE(im.ok());
    REQUIRE_EQ(im->size(), size_t(1));
    CHECK_EQ((*im)[0].date, std::string("1851.06.21"));
    auto game = pack.load((*im)[0].id);
    REQUIRE(game.ok());
    CHECK_EQ(game->tree.mainLine().size(), size_t(45));
    // 18. Bd6 carries a comment and the variation 18. Be3.
    const chess::GameNode* n = game->tree.root();
    for (int i = 0; i < 34; ++i) n = n->mainChild();  // after 17... Qxb2
    REQUIRE(n->children.size() == 2);
    CHECK_EQ(n->children[0]->comment, std::string("Giving up both rooks."));
    CHECK_EQ(n->children[1]->comment, std::string("is the quiet alternative."));
}

TEST_CASE("game pack: clock and eval commands are dropped; games keep their PGN only for real annotations") {
    auto db = storage::Database::open({}, storage::Database::Mode::Memory);
    REQUIRE(db.ok());
    GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    auto machine =
        chess::parsePgnGame("[White \"A\"]\n[Black \"B\"]\n\n1. e4 { [%eval 0.18] [%clk 1:00:50] } e5 { [%clk 0:59:10] } 1-0\n");
    auto human = chess::parsePgnGame("[White \"C\"]\n[Black \"D\"]\n\n1. e4 { [%clk 1:00:50] Best by test. } e5 1-0\n");
    auto engine = chess::parsePgnGame(
        "[White \"E\"]\n[Black \"F\"]\n\n1. e4 e5 2. Qh5 $6 {Inaccuracy. Nf3 was best.} (2. Nf3 Nc6) 2... Nc6 1-0\n");
    REQUIRE((machine.ok() && human.ok() && engine.ok()));
    REQUIRE(imp.add(*machine).ok());
    REQUIRE(imp.add(*engine).ok());
    REQUIRE(imp.add(*human).ok());
    REQUIRE(imp.finish({}).ok());
    CHECK_EQ(imp.stats().annotated, uint64_t(1));
    auto pack = GamePack::fromDatabase(std::move(*db), "t");
    REQUIRE(pack.ok());
    GameFilter f;
    f.player = "c";
    auto rows = pack->page(f, 0, 5);
    REQUIRE((rows.ok() && rows->size() == 1));
    auto g = pack->load((*rows)[0].id);
    REQUIRE(g.ok());
    CHECK_EQ(g->tree.root()->mainChild()->comment, std::string("Best by test."));
}

TEST_CASE("game pack: filters, counts and pages") {
    GamePack pack = samplePack();
    auto count = [&](const GameFilter& f) {
        auto c = pack.count(f);
        REQUIRE(c.ok());
        return *c;
    };
    GameFilter f;
    f.player = "Anderssen";
    CHECK_EQ(count(f), 2);
    f.opponent = "dufresne";
    CHECK_EQ(count(f), 1);
    GameFilter old;
    old.yearTo = 1900;
    CHECK_EQ(count(old), 3);
    GameFilter lichess;
    lichess.yearFrom = 2012;
    CHECK_EQ(count(lichess), 39);  // year from UTCDate
    GameFilter wins;
    wins.result = "1-0";
    GameFilter losses;
    losses.result = "0-1";
    GameFilter draws;
    draws.result = "1/2-1/2";
    CHECK_EQ(count(wins) + count(losses) + count(draws) <= 42, true);
    CHECK(count(wins) >= 3);
    GameFilter french;
    french.opening = "French";
    CHECK(count(french) >= 1);
    GameFilter c;
    c.eco = "C";
    CHECK(count(c) >= count(french));
    GameFilter ids;
    ids.ids = std::vector<int64_t>{1, 2, 3};
    CHECK_EQ(count(ids), 3);
    ids.ids = std::vector<int64_t>{};
    CHECK_EQ(count(ids), 0);

    // Newest first; consecutive pages do not overlap and cover everything.
    auto p1 = pack.page({}, 0, 20), p2 = pack.page({}, 20, 20), p3 = pack.page({}, 40, 20);
    REQUIRE(p1.ok());
    REQUIRE(p2.ok());
    REQUIRE(p3.ok());
    CHECK_EQ(p1->size() + p2->size() + p3->size(), size_t(42));
    std::set<int64_t> seen;
    for (const auto* p : {&*p1, &*p2, &*p3})
        for (const auto& r : *p) seen.insert(r.id);
    CHECK_EQ(seen.size(), size_t(42));
    CHECK(p3->back().date.rfind("18", 0) == 0);  // the 1850s games come last
}

TEST_CASE("move codec: one byte per move round-trips whole games and rejects bad blobs") {
    int games = 0;
    size_t plies = 0;
    for (const auto& g : chess::parsePgn(repoText("games-sample.pgn"))) {
        REQUIRE(g.ok());
        const auto moves = g->tree.mainLine();
        auto blob = chess::encodeMoveIndexes(g->tree.startPosition(), moves);
        REQUIRE(blob.ok());
        CHECK_EQ(blob->size(), moves.size());
        auto back = chess::decodeMoveIndexes(g->tree.startPosition(), *blob);
        REQUIRE(back.ok());
        CHECK((*back == moves));
        ++games;
        plies += moves.size();
    }
    CHECK_EQ(games, 39);
    MESSAGE("move codec: " + std::to_string(plies) + " plies, 1 byte each");
    const std::vector<uint8_t> bad{250};
    CHECK_FALSE(chess::decodeMoveIndexes(chess::Position::startPosition(), bad).ok());
}
