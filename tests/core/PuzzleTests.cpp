// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/MoveCodec.h"
#include "core/puzzles/PuzzleImport.h"
#include "core/util/Files.h"
#include "support/Test.h"

#include <set>
#include <string>
#include <vector>

using namespace pchess;
using namespace pchess::puzzles;

namespace {

// tests/fixtures/puzzles-sample.csv: the first 60 rows of the Lichess puzzle database (CC0).
std::string sampleCsv() {
    auto text = util::readTextFile(std::filesystem::path(test::fixtureDir()) / "puzzles-sample.csv");
    if (!text) MESSAGE(text.error().message());
    REQUIRE(text.ok());
    return *text;
}

void feed(PuzzleImporter& imp, const std::string& csv) {
    size_t i = 0;
    while (i < csv.size()) {
        size_t j = csv.find('\n', i);
        if (j == std::string::npos) j = csv.size();
        imp.addLine(std::string_view(csv).substr(i, j - i));
        i = j + 1;
    }
}

storage::Database importPack(const ImportFilter& f, ImportStats* stats = nullptr) {
    PuzzleImporter imp(f);
    feed(imp, sampleCsv());
    auto db = storage::Database::open({}, storage::Database::Mode::Memory);
    REQUIRE(db.ok());
    auto n = imp.write(*db, {{"input", "fixture"}});
    if (!n) MESSAGE(n.error().message());
    REQUIRE(n.ok());
    if (stats) *stats = imp.stats();
    return std::move(*db);
}

std::vector<std::string> ids(storage::Database& db) {
    auto s = db.prepare("SELECT id FROM puzzles ORDER BY id");
    REQUIRE(s.ok());
    std::vector<std::string> out;
    for (auto row = s->step(); row.ok() && *row; row = s->step()) out.push_back(s->text(0));
    return out;
}

} // namespace

TEST_CASE("puzzle import: the sample pack has every row, valid move blobs, themes and meta") {
    ImportFilter f;
    f.count = 1000;
    ImportStats st;
    storage::Database db = importPack(f, &st);
    CHECK_EQ(st.rows, uint64_t(60));
    CHECK_EQ(st.rejected, uint64_t(0));
    CHECK_EQ(ids(db).size(), size_t(60));

    // Every stored line replays legally from its FEN (the codec checks each move).
    auto s = db.prepare("SELECT fen, moves, rating, themes FROM puzzles ORDER BY rating");
    REQUIRE(s.ok());
    int previous = 0, rows = 0;
    for (auto row = s->step(); row.ok() && *row; row = s->step()) {
        auto pos = chess::parseFen(s->text(0));
        REQUIRE(pos.ok());
        const auto blob = s->blob(1);
        auto moves = chess::decodeMoves(pos.value(), blob);
        CHECK(moves.ok());
        CHECK(moves.ok() && moves->size() >= 2);
        CHECK(s->integer(2) >= previous);  // sorted by rating
        previous = s->integer(2);
        CHECK(s->int64(3) != 0);  // every Lichess puzzle has themes
        ++rows;
    }
    CHECK_EQ(rows, 60);

    auto themes = db.prepare("SELECT count(*), max(bit) FROM themes");
    REQUIRE(themes->step().ok());
    CHECK(themes->integer(0) > 5);
    CHECK(themes->integer(1) < 64);
    auto meta = db.prepare("SELECT value FROM meta WHERE key = 'count'");
    REQUIRE(meta->step().ok());
    CHECK_EQ(meta->text(0), std::string("60"));
}

TEST_CASE("puzzle import: sampling is deterministic per seed and filters apply") {
    ImportFilter f;
    f.count = 10;
    f.seed = 7;
    storage::Database a = importPack(f);
    storage::Database b = importPack(f);
    CHECK(ids(a) == ids(b));
    CHECK_EQ(ids(a).size(), size_t(10));
    f.seed = 8;
    storage::Database c = importPack(f);
    CHECK(ids(a) != ids(c));

    ImportFilter g;
    g.count = 1000;
    g.minRating = 1500;
    g.includeThemes = {"endgame"};
    ImportStats st;
    storage::Database d = importPack(g, &st);
    CHECK(st.filtered > 0);
    auto s = d.prepare("SELECT min(rating), count(*) FROM puzzles");
    REQUIRE(s->step().ok());
    CHECK(s->integer(0) >= 1500);
    CHECK_EQ(uint64_t(s->integer(1)), st.candidates);
    auto bit = d.prepare("SELECT bit FROM themes WHERE name = 'endgame'");
    REQUIRE(bit->step().ok());
    const int64_t mask = int64_t(1) << bit->integer(0);
    auto all = d.prepare("SELECT count(*) FROM puzzles WHERE (themes & ?1) = 0");
    all->bind(1, mask);
    REQUIRE(all->step().ok());
    CHECK_EQ(all->integer(0), 0);
}

TEST_CASE("puzzle import: malformed rows are rejected with line numbers, the rest continues") {
    PuzzleImporter imp(ImportFilter{});
    imp.addLine("PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags");
    imp.addLine("bad1,not a fen,e2e4 e7e5,1500,80,90,100,short,url,");
    imp.addLine("bad2,4k3/8/8/8/8/8/8/4K3 w - - 0 1,e2e4 e7e5,1500,80,90,100,short,url,");  // no pawn on e2
    imp.addLine("bad3,only,three");
    imp.addLine("ok1,4k3/8/8/8/8/8/8/R3K3 w - - 0 1,a1a7 e8f8,1200,80,90,100,short,url,\r");
    CHECK_EQ(imp.stats().rows, uint64_t(4));
    CHECK_EQ(imp.stats().rejected, uint64_t(3));
    CHECK_EQ(imp.stats().candidates, uint64_t(1));
    REQUIRE_EQ(imp.stats().firstRejects.size(), size_t(3));
    CHECK(imp.stats().firstRejects[0].rfind("line 2: bad1", 0) == 0);
    CHECK(imp.stats().firstRejects[2].find("fields") != std::string::npos);
}
