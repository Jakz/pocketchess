// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/DrillScreen.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "core/chess/Fen.h"
#include "core/chess/Pgn.h"
#include "core/chess/San.h"
#include "core/games/GamePack.h"
#include "core/games/OpeningDrill.h"
#include "core/storage/ChessDb.h"
#include "core/util/Files.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>
#include <map>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;
using games::OpeningDrill;

namespace {

chess::Move uci(const chess::Position& p, const char* text) {
    auto m = chess::parseUci(p, text);
    REQUIRE(m.ok());
    return *m;
}

// A tiny book keyed by FEN: 1. e4 (60) / d4 (30) / Nf3 (10) / a3 (1), 1... e5 only, 2. Nf3 (40) / Bc4 (5).
OpeningDrill::BookFn tinyBook() {
    auto book = std::make_shared<std::map<std::string, std::vector<games::BookMove>>>();
    const chess::Position s = chess::Position::startPosition();
    const chess::Position e4 = s.after(uci(s, "e2e4"));
    const chess::Position e5 = e4.after(uci(e4, "e7e5"));
    (*book)[chess::toFen(s)] = {{uci(s, "e2e4"), 60}, {uci(s, "d2d4"), 30}, {uci(s, "g1f3"), 10}, {uci(s, "a2a3"), 1}};
    (*book)[chess::toFen(e4)] = {{uci(e4, "e7e5"), 50}};
    (*book)[chess::toFen(e5)] = {{uci(e5, "g1f3"), 40}, {uci(e5, "f1c4"), 5}};
    return [book](const chess::Position& p) {
        auto it = book->find(chess::toFen(p));
        return it == book->end() ? std::vector<games::BookMove>{} : it->second;
    };
}

// <root>/content/games/sample.db: the 39 Lichess fixture games and the classics (with a book table).
std::filesystem::path bookRoot(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto file = root / "content" / "games" / "sample.db";
    std::filesystem::create_directories(file.parent_path(), ec);
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    REQUIRE(db->exec("PRAGMA journal_mode=DELETE").ok());
    games::GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    for (const char* f : {"games-sample.pgn", "classics.pgn"}) {
        std::filesystem::path pgn = std::filesystem::path(test::fixtureDir()) / f;
        if (!std::filesystem::exists(pgn))
            pgn = std::filesystem::path(test::fontDir()).parent_path() / "samples" / "pgn" / f;
        auto text = util::readTextFile(pgn);
        REQUIRE(text.ok());
        for (auto& g : chess::parsePgn(*text)) REQUIRE(imp.add(*g).ok());
    }
    REQUIRE(imp.finish({{"title", "Sample games"}}).ok());
    return root;
}

} // namespace

TEST_CASE("opening drill: book moves, the main move, misses, the opponent's book replies and the end") {
    util::Random rnd(7);
    const chess::Position s = chess::Position::startPosition();
    OpeningDrill d(s, chess::White, tinyBook(), rnd);
    REQUIRE(d.userToMove());
    CHECK_EQ(d.games(), 101);
    CHECK_EQ(d.candidates().size(), size_t(3));  // a3 (1 game, < 3%) is not worth knowing
    CHECK(d.guess(uci(s, "a2a3")) == OpeningDrill::Verdict::Wrong);
    CHECK(d.hint() == chess::E2);
    CHECK(d.guess(uci(s, "d2d4")) == OpeningDrill::Verdict::Book);
    CHECK_EQ(d.lastSharePct(), 30);
    CHECK(d.finished());  // nothing known after 1. d4
    CHECK_EQ(d.score().book, 1);

    OpeningDrill w(s, chess::White, tinyBook(), rnd);
    CHECK(w.guess(uci(s, "e2e4")) == OpeningDrill::Verdict::Main);
    CHECK(w.lastOutcome() == OpeningDrill::Outcome::Main);
    CHECK_FALSE(w.userToMove());
    REQUIRE(w.opponentMove());
    CHECK_EQ(chess::toUci(w.played().back()), std::string("e7e5"));
    const chess::Position p = w.position();
    CHECK(w.guess(uci(p, "h2h3")) == OpeningDrill::Verdict::Wrong);
    CHECK(w.guess(uci(p, "h2h4")) == OpeningDrill::Verdict::Shown);  // the second miss shows 2. Nf3
    CHECK_EQ(chess::toUci(w.played().back()), std::string("g1f3"));
    CHECK(w.finished());
    CHECK_EQ(w.score().main, 1);
    CHECK_EQ(w.score().missed, 1);

    OpeningDrill b(s, chess::Black, tinyBook(), rnd);
    CHECK_FALSE(b.userToMove());
    CHECK(b.guess(uci(s, "e2e4")) == OpeningDrill::Verdict::NotYourTurn);
    REQUIRE(b.opponentMove());  // White's first move, weighted by games
    CHECK(b.played().size() == 1);
}

TEST_CASE("opening drill screen: the sample pack's book, replies, the end, and chess.db") {
    const auto root = bookRoot("pchess-drill");
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    AppConfig cfg;
    cfg.root = root;
    cfg.fontDir = test::fontDir();
    cfg.logToFile = false;
    cfg.persistSettings = true;
    auto app = std::make_unique<Application>(host, presenter, cfg);
    app->start();
    app->random().reseed(11);
    REQUIRE(app->bookPack() != nullptr);

    app->push(std::make_unique<DrillScreen>(*app, std::vector<chess::Move>{}, chess::White));
    app->flush();
    auto* d = dynamic_cast<DrillScreen*>(app->top());
    REQUIRE(d != nullptr);
    REQUIRE(d->ready());
    REQUIRE(d->drill()->userToMove());
    REQUIRE_FALSE(d->drill()->candidates().empty());
    const std::string main = chess::toUci(d->drill()->candidates().front().move);
    CHECK(d->playUci(main));
    CHECK(d->status().find("the main move") != std::string::npos);
    host.advance(600ms);
    app->flush();
    CHECK(d->info().find("Black played 1…") != std::string::npos);
    app->flush();
    test::checkGolden("drill", app->canvas());

    // Show the moves until the book ends.
    for (int i = 0; i < 40 && !d->drill()->finished(); ++i) {
        d->reveal();
        host.advance(600ms);
        app->flush();
    }
    REQUIRE(d->drill()->finished());
    CHECK(d->status().rfind("End of the book", 0) == 0);

    storage::Database* db = app->userDb();
    auto run = db->prepare("SELECT main, missed, finished, moves FROM drill_runs WHERE id = ?1");
    REQUIRE(run.ok());
    run->bind(1, d->runId());
    REQUIRE(*run->step());
    CHECK_EQ(run->integer(0), 1);
    CHECK_EQ(run->integer(1), d->drill()->score().missed);
    CHECK_EQ(run->integer(2), 1);
    CHECK_EQ(run->integer(3), static_cast<int>(d->drill()->played().size()));
    auto moves = db->prepare("SELECT count(*) FROM drill_moves WHERE run_id = ?1");
    REQUIRE(moves.ok());
    moves->bind(1, d->runId());
    REQUIRE(*moves->step());
    CHECK_EQ(moves->integer(0), d->drill()->score().moves());

    // Menu -> Analyze from here: the same position.
    const std::string fen = chess::toFen(d->drill()->position());
    REQUIRE(runScript(*app, "key:menu;key:ok").ok());  // "Analyze from here"
    app->flush();
    auto* a = dynamic_cast<AnalyzeScreen*>(app->top());
    REQUIRE(a != nullptr);
    CHECK_EQ(chess::toFen(a->position()), fen);
    app.reset();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
