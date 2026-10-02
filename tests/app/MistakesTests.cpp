// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/HeadlessHost.h"
#include "app/HomeScreen.h"
#include "app/TacticsScreen.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/puzzles/MistakeFinder.h"
#include "core/storage/ChessDb.h"
#include "support/FakeEngine.h"
#include "support/Test.h"

#include <algorithm>
#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;

namespace {

// 1. e4 e5 2. Qh5 Nf6 3. b3?? Nxh5: White leaves the queen en prise.
constexpr const char* kBlunderPgn = R"([Event "Play"]
[White "You"]
[Black "Engine"]
[Result "0-1"]

1. e4 e5 2. Qh5 Nf6 3. b3 Nxh5 0-1
)";

// A one-ply "engine": material plus the best capture for the side to move, in centipawns.
int materialEval(const chess::Position& p) {
    static constexpr int kValue[] = {100, 300, 300, 500, 900, 0};
    int score = 0;
    for (int s = 0; s < 64; ++s) {
        const chess::Piece pc = p.pieceAt(static_cast<chess::Square>(s));
        if (pc == chess::NoPiece) continue;
        score += (chess::colorOf(pc) == p.sideToMove() ? 1 : -1) * kValue[chess::typeOf(pc)];
    }
    int best = 0;
    for (chess::Move m : chess::legalMoves(p))
        if (m.isCapture() && p.pieceAt(m.to()) != chess::NoPiece) best = std::max(best, kValue[chess::typeOf(p.pieceAt(m.to()))]);
    return score + best;
}

std::vector<chess::Move> line(std::initializer_list<const char*> sans) {
    std::vector<chess::Move> out;
    chess::Position p = chess::Position::startPosition();
    for (const char* s : sans) {
        auto m = chess::parseSan(p, s);
        REQUIRE(m.ok());
        out.push_back(*m);
        p.makeMove(*m);
    }
    return out;
}

} // namespace

TEST_CASE("mistakes: winning chances and the moves that throw them away") {
    CHECK_EQ(puzzles::winChance({engine::Score::Kind::Cp, 0}), 50);
    CHECK(puzzles::winChance({engine::Score::Kind::Cp, 900}) > 90);
    CHECK(puzzles::winChance({engine::Score::Kind::Mate, -3}) < 1);
    CHECK_EQ(puzzles::winChance({engine::Score::Kind::Mate, 2}), puzzles::winChance({engine::Score::Kind::Cp, 5000}));

    const auto moves = line({"e4", "e5", "Qh5", "Nf6", "b3", "Nxh5"});
    const chess::Position start = chess::Position::startPosition();
    std::vector<puzzles::PlyEval> evals(moves.size() + 1);
    chess::Position p = start;
    for (size_t i = 0; i <= moves.size(); ++i) {
        evals[i].score = engine::Score{engine::Score::Kind::Cp, materialEval(p)};
        evals[i].best = chess::legalMoves(p)[0];
        if (i < moves.size()) p.makeMove(moves[i]);
    }
    evals[4].best = chess::parseSan(start.after(moves[0]).after(moves[1]).after(moves[2]).after(moves[3]), "Qxf7+").value();
    const auto found = puzzles::findMistakes(start, moves, chess::White, evals);
    REQUIRE_EQ(found.size(), size_t(1));
    CHECK_EQ(found[0].ply, 4);
    CHECK_EQ(chess::toUci(found[0].played), std::string("b2b3"));
    CHECK_EQ(chess::toUci(found[0].moves[0]), std::string("g8f6"));
    CHECK_EQ(chess::toUci(found[0].moves[1]), std::string("h5f7"));
    CHECK(found[0].before - found[0].after >= 20);
    // The puzzle starts before Black's 2... Nf6.
    CHECK_EQ(found[0].fen, chess::toFen(start.after(moves[0]).after(moves[1]).after(moves[2])));
    // Black did nothing wrong by these numbers; the same move as the engine's is never a mistake.
    CHECK(puzzles::findMistakes(start, moves, chess::Black, evals).empty());
    evals[4].best = moves[4];
    CHECK(puzzles::findMistakes(start, moves, chess::White, evals).empty());
}

TEST_CASE("mistakes: storage keeps found mistakes and checks each game once") {
    auto db = storage::Database::open("", storage::Database::Mode::Memory);
    REQUIRE(db.ok());
    REQUIRE(storage::migrate(*db, storage::chessDbMigrations()).ok());
    storage::PlayedGame g;
    g.date = 10;
    g.pgn = kBlunderPgn;
    g.result = "0-1";
    g.human = "white";
    auto id = storage::addPlayedGame(*db, g);
    REQUIRE(id.ok());
    g.result = "*";  // unfinished: never checked
    REQUIRE(storage::addPlayedGame(*db, g).ok());
    CHECK_EQ(storage::uncheckedPlayedGames(*db).value().size(), size_t(1));
    storage::StoredMistake m;
    m.ply = 4;
    m.fen = "fen";
    m.moves = "g8f6 a2a3";
    m.played = "b2b3";
    m.before = 59;
    m.after = 4;
    REQUIRE(storage::saveMistakeScan(*db, *id, {m}, "fakefish", 100).ok());
    CHECK(storage::uncheckedPlayedGames(*db).value().empty());
    auto all = storage::mistakes(*db);
    REQUIRE(all.ok());
    REQUIRE_EQ(all->size(), size_t(1));
    CHECK_EQ((*all)[0].gameId, *id);
    CHECK_EQ((*all)[0].moves, std::string("g8f6 a2a3"));
    REQUIRE(storage::saveMistakeScan(*db, *id, {m}, "fakefish", 200).ok());  // again: no duplicate
    CHECK_EQ(storage::mistakes(*db).value().size(), size_t(1));
}

TEST_CASE("mistakes: Home -> Puzzles -> My mistakes checks new games with the engine, then trains them") {
    const auto root = std::filesystem::temp_directory_path() / "pchess-mistakes";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    test::FakeEngineFactory fake;
    fake.behaviour.evalCp = materialEval;
    AppConfig cfg;
    cfg.root = root;
    cfg.fontDir = test::fontDir();
    cfg.logToFile = false;
    cfg.persistSettings = true;
    cfg.extraEngines.push_back(test::fakeDescriptor("fakefish"));
    cfg.engineFactory = [&fake](const engine::EngineDescriptor& d, util::Scheduler& s) {
        return std::make_unique<engine::UciEngine>(d, s, test::selfExe(), fake.factory());
    };
    auto app = std::make_unique<Application>(host, presenter, cfg);
    app->start();
    auto pump = [&](int rounds) {
        for (int i = 0; i < rounds; ++i) {
            host.advance(50ms);
            app->flush();
        }
    };

    storage::PlayedGame g;
    g.date = 1'800'000'000;
    g.pgn = kBlunderPgn;
    g.result = "0-1";
    g.human = "white";
    REQUIRE(storage::addPlayedGame(*app->userDb(), g).ok());

    auto* home = dynamic_cast<HomeScreen*>(app->top());
    REQUIRE(home != nullptr);
    home->openMistakes();
    app->flush();
    CHECK_EQ(std::string(app->top()->name()), std::string("message"));  // the progress dialog
    for (int i = 0; i < 200 && dynamic_cast<TacticsScreen*>(app->top()) == nullptr; ++i) pump(1);
    auto* t = dynamic_cast<TacticsScreen*>(app->top());
    REQUIRE(t != nullptr);
    CHECK(t->mode() == TacticsScreen::Mode::Mistakes);
    CHECK(storage::uncheckedPlayedGames(*app->userDb()).value().empty());
    REQUIRE_EQ(storage::mistakes(*app->userDb()).value().size(), size_t(1));

    pump(20);  // the opponent's move from the game (2... Nf6)
    REQUIRE(t->session() != nullptr);
    CHECK_EQ(chess::toUci(t->session()->played().back()), std::string("g8f6"));
    CHECK(t->playUci("a2a3"));  // the fake engine's move: the first legal move in UCI order
    CHECK(t->session()->finished());
    CHECK(t->info().find("you played b3") != std::string::npos);
    auto state = app->progress()->state("m1");
    REQUIRE(state.ok());
    REQUIRE(state->has_value());
    CHECK_EQ((*state)->attempts, 1);

    // Nothing new to check: the screen opens straight away, with nothing left to solve.
    app->pop();
    app->flush();
    home->openMistakes();
    app->flush();
    t = dynamic_cast<TacticsScreen*>(app->top());
    REQUIRE(t != nullptr);
    CHECK_EQ(t->status(), std::string("No mistakes to solve"));
    app.reset();
    std::filesystem::remove_all(root, ec);
}
