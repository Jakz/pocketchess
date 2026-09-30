// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/Fen.h"
#include "core/chess/MoveCodec.h"
#include "core/chess/San.h"
#include "core/puzzles/PuzzleImport.h"
#include "core/puzzles/PuzzlePack.h"
#include "core/puzzles/PuzzleSession.h"
#include "core/puzzles/Progress.h"
#include "core/util/Files.h"
#include "support/Test.h"

#include <cstdlib>
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

namespace {

PuzzlePack samplePack() {
    ImportFilter f;
    f.count = 1000;
    auto pack = PuzzlePack::fromDatabase(importPack(f), "sample");
    REQUIRE(pack.ok());
    return std::move(*pack);
}

} // namespace

TEST_CASE("puzzle pack: count, themes and lookup by id") {
    PuzzlePack pack = samplePack();
    CHECK_EQ(pack.count(), 60);
    CHECK_EQ(pack.name(), std::string("sample"));
    const uint64_t endgame = pack.themeMask({"endgame", "noSuchTheme"});
    REQUIRE(endgame != 0);
    CHECK(pack.themesOf(endgame) == std::vector<std::string>{"endgame"});

    auto p = pack.byId("00008");
    REQUIRE(p.ok());
    REQUIRE(p->has_value());
    CHECK_EQ((*p)->rating, 1797);
    CHECK_EQ((*p)->moves.size(), size_t(6));
    CHECK_EQ(chess::toUci((*p)->moves[0]), std::string("f2g3"));
    auto none = pack.byId("zzzzz");
    REQUIRE(none.ok());
    CHECK_FALSE(none->has_value());
}

TEST_CASE("puzzle selector: deterministic per seed, near the rating, widening, skipping and themes") {
    PuzzlePack pack = samplePack();
    auto sequence = [&](uint64_t seed) {
        util::Random rnd(seed);
        PuzzleSelector sel(pack, rnd);
        std::vector<std::string> out;
        for (int i = 0; i < 5; ++i) {
            auto p = sel.next({1500});
            REQUIRE(p.ok());
            REQUIRE(p->has_value());
            out.push_back((*p)->id);
        }
        return out;
    };
    CHECK(sequence(42) == sequence(42));

    util::Random rnd(3);
    PuzzleSelector sel(pack, rnd);
    for (int i = 0; i < 20; ++i) {  // the sample has puzzles within ±150 of 1500
        auto p = sel.next({1500});
        REQUIRE(p.ok());
        REQUIRE(p->has_value());
        CHECK(std::abs((*p)->rating - 1500) <= 150);
    }

    auto far = sel.next({3900});  // nothing near: the window widens to any rating
    REQUIRE(far.ok());
    CHECK(far->has_value());

    // Skip everything except one puzzle: it is found wherever it is.
    auto only = sel.next({1500, 0, 0, [](const std::string& id) { return id != "00008"; }});
    REQUIRE(only.ok());
    REQUIRE(only->has_value());
    CHECK_EQ((*only)->id, std::string("00008"));
    auto nothing = sel.next({1500, 0, 0, [](const std::string&) { return true; }});
    REQUIRE(nothing.ok());
    CHECK_FALSE(nothing->has_value());

    const uint64_t endgame = pack.themeMask({"endgame"});
    for (int i = 0; i < 10; ++i) {
        auto p = sel.next({1500, endgame, 0, {}});
        REQUIRE(p.ok());
        REQUIRE(p->has_value());
        CHECK(((*p)->themes & endgame) != 0);
        auto q = sel.next({1500, 0, endgame, {}});
        REQUIRE(q.ok());
        REQUIRE(q->has_value());
        CHECK(((*q)->themes & endgame) == 0);
    }
}

TEST_CASE("rating: Elo expectation and K=24 binary updates") {
    CHECK(std::abs(expectedScore(1500, 1500) - 0.5) < 1e-9);
    CHECK(std::abs(expectedScore(1900, 1500) - 0.9090909) < 1e-6);
    CHECK_EQ(updatedRating(1500, 1500, true), 1512);
    CHECK_EQ(updatedRating(1500, 1500, false), 1488);
    CHECK_EQ(updatedRating(1500, 1900, true), 1522);  // beating a harder puzzle gains more
    CHECK_EQ(updatedRating(1500, 1100, false), 1478);
}

TEST_CASE("review schedule: 1 -> 3 -> 7 -> 21 -> 60 days, a failure resets, first-try solves stay out") {
    constexpr int64_t day = 86400, t0 = 1'800'000'000;
    ReviewState s = scheduleAfter({}, true, t0);
    CHECK_EQ(s.intervalDays, 0);
    CHECK_FALSE(s.nextReview.has_value());

    s = scheduleAfter({}, false, t0);
    CHECK_EQ(s.intervalDays, 1);
    CHECK_EQ(s.nextReview.value_or(0), t0 + day);
    const int expected[] = {3, 7, 21, 60, 60};
    int64_t t = t0;
    for (int days : expected) {
        t += day;
        s = scheduleAfter(s, true, t);
        CHECK_EQ(s.intervalDays, days);
        CHECK_EQ(s.nextReview.value_or(0), t + days * day);
    }
    s = scheduleAfter(s, false, t);
    CHECK_EQ(s.intervalDays, 1);
    CHECK_EQ(s.attempts, 7);
    CHECK_EQ(s.failures, 2);
    CHECK_EQ(s.successes, 5);
}

TEST_CASE("progress: rated attempts move the rating; failures come back for review on the day") {
    auto mem = storage::Database::open({}, storage::Database::Mode::Memory);
    REQUIRE(mem.ok());
    auto progress = Progress::fromDatabase(std::move(*mem));
    REQUIRE(progress.ok());
    Progress& pr = *progress;
    constexpr int64_t day = 86400, t0 = 1'800'000'000;
    CHECK_EQ(pr.rating(), kInitialRating);

    Puzzle a;
    a.id = "A";
    a.rating = 1500;
    Puzzle b;
    b.id = "B";
    b.rating = 1500;
    auto r1 = pr.record("sample", a, true, true, t0);
    REQUIRE(r1.ok());
    CHECK_EQ(r1->ratingAfter, 1512);
    auto r2 = pr.record("sample", b, false, true, t0 + 60);
    REQUIRE(r2.ok());
    CHECK_EQ(r2->ratingBefore, 1512);
    CHECK_EQ(pr.rating(), r2->ratingAfter);
    CHECK(r2->ratingAfter < 1512);

    auto seen = pr.seenIds();
    REQUIRE(seen.ok());
    CHECK_EQ(seen->size(), size_t(2));
    CHECK_EQ(pr.dueCount(t0 + day - 1).value(), 0);
    CHECK_EQ(pr.dueCount(t0 + day + 60).value(), 1);
    auto due = pr.dueIds(t0 + 2 * day, 10);
    REQUIRE(due.ok());
    REQUIRE_EQ(due->size(), size_t(1));
    CHECK_EQ((*due)[0], std::string("B"));

    // The review attempt reschedules without moving the rating.
    const int before = pr.rating();
    auto r3 = pr.record("sample", b, true, false, t0 + 2 * day);
    REQUIRE(r3.ok());
    CHECK_EQ(pr.rating(), before);
    CHECK_EQ(r3->state.intervalDays, 3);
    CHECK_EQ(pr.dueCount(t0 + 2 * day).value(), 0);
    auto st = pr.state("B");
    REQUIRE(st.ok());
    REQUIRE(st->has_value());
    CHECK_EQ((*st)->attempts, 2);
    auto hist = pr.history(10);
    REQUIRE(hist.ok());
    CHECK_EQ(hist->size(), size_t(2));  // only rated attempts
    CHECK_EQ((*hist)[0].rating, before);
}

namespace {

Puzzle handMade(const std::string& fen, std::initializer_list<const char*> ucis) {
    Puzzle p;
    p.id = "hand";
    p.fen = fen;
    chess::Position pos = chess::parseFen(fen).value();
    for (const char* u : ucis) {
        const chess::Move m = chess::parseUci(pos, u).value();
        p.moves.push_back(m);
        pos.makeMove(m);
    }
    return p;
}

chess::Move uci(const PuzzleSession& s, const char* text) { return chess::parseUci(s.position(), text).value(); }

} // namespace

TEST_CASE("puzzle session: the stored line, wrong moves, retry, hint and giving up") {
    PuzzlePack pack = samplePack();
    auto p = pack.byId("00008");  // f2g3 e6e7 b2b1 b3c1 b1c1 h6c1 (user plays White)
    REQUIRE(p.ok());
    PuzzleSession s(**p);
    REQUIRE(s.valid());
    CHECK(s.userColor() == chess::White);
    CHECK_EQ(s.userMovesTotal(), 3);
    CHECK(s.state() == PuzzleSession::State::OpponentToMove);
    CHECK(s.tryMove(chess::Move::none()) == PuzzleSession::Verdict::NotYourTurn);
    CHECK_EQ(chess::toUci(s.pendingOpponentMove()), std::string("f2g3"));
    s.playOpponentMove();
    CHECK(s.state() == PuzzleSession::State::AwaitUser);

    // A legal but wrong move is not played; retry returns to the same position.
    const std::string before = chess::toFen(s.position());
    CHECK(s.tryMove(uci(s, "h6h7")) == PuzzleSession::Verdict::Wrong);
    CHECK(s.state() == PuzzleSession::State::Wrong);
    CHECK_EQ(chess::toFen(s.position()), before);
    s.retry();
    CHECK(s.tryMove(uci(s, "e6e7")) == PuzzleSession::Verdict::Correct);
    CHECK_EQ(s.userMovesDone(), 1);
    s.playOpponentMove();
    CHECK(s.tryMove(uci(s, "b3c1")) == PuzzleSession::Verdict::Correct);
    s.playOpponentMove();
    CHECK(s.tryMove(uci(s, "h6c1")) == PuzzleSession::Verdict::Solved);
    CHECK(s.finished());
    CHECK_FALSE(s.solvedClean());  // one wrong move: counts as failed for the rating
    CHECK_EQ(s.played().size(), size_t(6));

    PuzzleSession h(**p);
    h.playOpponentMove();
    CHECK_EQ(chess::toUci(h.hint()), std::string("e6e7"));
    CHECK(h.hintUsed());
    CHECK(h.tryMove(uci(h, "e6e7")) == PuzzleSession::Verdict::Correct);

    PuzzleSession g(**p);
    g.playOpponentMove();
    g.giveUp();
    CHECK(g.state() == PuzzleSession::State::Failed);
    int shown = 0;
    while (g.nextSolutionMove()) ++shown;
    CHECK_EQ(shown, 5);  // the rest of the line after f2g3
    CHECK_EQ(g.played().size(), size_t(6));
}

TEST_CASE("puzzle session: any checkmating move solves (mate-in-1 exception); invalid lines are refused") {
    // After ...b6, both Re8# (the stored move) and Ra8# mate.
    Puzzle p = handMade("6k1/1p3ppp/8/8/8/8/5PPP/R3R1K1 b - - 0 1", {"b7b6", "e1e8"});
    PuzzleSession clean(p);
    REQUIRE(clean.valid());
    clean.playOpponentMove();
    CHECK(clean.tryMove(uci(clean, "a1a8")) == PuzzleSession::Verdict::Solved);
    CHECK(clean.solvedClean());
    CHECK_FALSE(clean.nextSolutionMove());  // the stored line no longer applies

    PuzzleSession other(p);
    other.playOpponentMove();
    CHECK(other.tryMove(uci(other, "a1a7")) == PuzzleSession::Verdict::Wrong);  // no mate

    Puzzle broken = p;
    broken.moves = {p.moves[1], p.moves[0]};  // White cannot move first here
    CHECK_FALSE(PuzzleSession(broken).valid());
    Puzzle badFen = p;
    badFen.fen = "not a fen";
    CHECK_FALSE(PuzzleSession(badFen).valid());
}
