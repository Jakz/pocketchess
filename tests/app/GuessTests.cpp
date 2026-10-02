// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/GameStudyScreen.h"
#include "app/GuessScreen.h"
#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "core/chess/Pgn.h"
#include "core/chess/San.h"
#include "core/games/GamePack.h"
#include "core/games/GuessSession.h"
#include "core/storage/ChessDb.h"
#include "core/util/Files.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;
using games::GuessSession;

namespace {

std::vector<chess::Move> sanLine(const chess::Position& start, std::initializer_list<const char*> sans) {
    std::vector<chess::Move> out;
    chess::Position p = start;
    for (const char* s : sans) {
        auto m = chess::parseSan(p, s);
        REQUIRE(m.ok());
        out.push_back(*m);
        p.makeMove(*m);
    }
    return out;
}

chess::Move uci(const chess::Position& p, const char* text) {
    auto m = chess::parseUci(p, text);
    REQUIRE(m.ok());
    return *m;
}

// <root>/content/games/sample.db with the three classics (the Immortal, the Evergreen, the Opera game).
std::filesystem::path classicsRoot(const char* name) {
    const auto root = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const auto file = root / "content" / "games" / "sample.db";
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path pgn = std::filesystem::path(test::fixtureDir()) / "classics.pgn";
    if (!std::filesystem::exists(pgn))
        pgn = std::filesystem::path(test::fontDir()).parent_path() / "samples" / "pgn" / "classics.pgn";
    auto text = util::readTextFile(pgn);
    REQUIRE(text.ok());
    auto db = storage::Database::open(file, storage::Database::Mode::ReadWrite);
    REQUIRE(db.ok());
    REQUIRE(db->exec("PRAGMA journal_mode=DELETE").ok());
    games::GameImporter imp(*db, nullptr);
    REQUIRE(imp.begin().ok());
    for (auto& g : chess::parsePgn(*text)) REQUIRE(imp.add(*g).ok());
    REQUIRE(imp.finish({{"title", "Classics"}}).ok());
    return root;
}

struct GuessHarness {
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    std::unique_ptr<Application> app;

    explicit GuessHarness(const std::filesystem::path& root) {
        AppConfig cfg;
        cfg.root = root;
        cfg.fontDir = test::fontDir();
        cfg.logToFile = false;
        cfg.persistSettings = true;  // runs live in user/chess.db
        app = std::make_unique<Application>(host, presenter, cfg);
        app->start();
    }
    GuessScreen* guess() const { return dynamic_cast<GuessScreen*>(app->top()); }
    void run(const std::string& script) {
        auto r = runScript(*app, script);
        if (!r) MESSAGE(r.error().message());
        REQUIRE(r.ok());
        app->flush();
    }
    void pump(std::chrono::milliseconds ms) {
        host.advance(ms);
        app->flush();
    }
    int64_t immortal() const {
        games::GameFilter f;
        f.player = "kieseritzky";
        auto rows = app->gamePack()->page(f, 0, 1);
        REQUIRE(rows.ok());
        REQUIRE_EQ(rows->size(), size_t(1));
        return rows->front().id;
    }
};

} // namespace

TEST_CASE("guess session: first try, hint, misses, reveal and the score") {
    const chess::Position start = chess::Position::startPosition();
    const auto line = sanLine(start, {"e4", "e5", "Bc4", "Nc6", "Qh5", "Nf6", "Qxf7#"});
    GuessSession s(start, line, GuessSession::Side::White);
    REQUIRE(s.userToMove());
    CHECK(s.guess(uci(s.position(), "e2e4")) == GuessSession::Verdict::Match);
    CHECK(s.lastOutcome() == GuessSession::Outcome::First);
    CHECK_FALSE(s.userToMove());
    CHECK(s.guess(uci(s.position(), "d7d5")) == GuessSession::Verdict::NotYourTurn);
    CHECK(s.advance());  // ...e5 plays itself
    CHECK_FALSE(s.advance());

    CHECK(s.guess(uci(s.position(), "d2d4")) == GuessSession::Verdict::Wrong);
    CHECK_EQ(s.misses(), 1);
    CHECK(s.hint() == chess::F1);
    CHECK(s.guess(uci(s.position(), "f1c4")) == GuessSession::Verdict::Match);
    CHECK(s.lastOutcome() == GuessSession::Outcome::Second);
    CHECK_EQ(s.misses(), 0);
    CHECK_FALSE(s.hinted());
    s.advance();

    CHECK(s.guess(uci(s.position(), "g1f3")) == GuessSession::Verdict::Wrong);
    CHECK(s.guess(uci(s.position(), "b1c3")) == GuessSession::Verdict::Revealed);  // 2 tries: shown
    CHECK(s.lastOutcome() == GuessSession::Outcome::Missed);
    CHECK_EQ(chess::toUci(s.lastMove()), std::string("d1h5"));
    s.advance();
    s.reveal();
    CHECK(s.finished());
    CHECK_FALSE(s.userToMove());
    CHECK_EQ(s.score().first, 1);
    CHECK_EQ(s.score().second, 1);
    CHECK_EQ(s.score().missed, 2);
    CHECK_EQ(s.score().percent(), 37);  // (2 + 1) / 8 points

    // Black's side: White's first move is not the user's; a resumed run starts at its ply with its score.
    GuessSession b(start, line, GuessSession::Side::Black, 2, {3, 0, 1});
    CHECK_EQ(b.ply(), 2);
    CHECK_FALSE(b.userToMove());
    CHECK(b.advance());
    CHECK(b.userToMove());
    CHECK_EQ(b.score().first, 3);
    GuessSession both(start, line, GuessSession::Side::Both);
    CHECK(both.guess(uci(both.position(), "e2e4")) == GuessSession::Verdict::Match);
    CHECK(both.userToMove());
}

TEST_CASE("storage: guess runs and moves are saved, updated and totalled") {
    auto db = storage::Database::open("", storage::Database::Mode::Memory);
    REQUIRE(db.ok());
    REQUIRE(storage::migrate(*db, storage::chessDbMigrations()).ok());
    storage::GuessRun run;
    run.pack = "classics";
    run.gameId = 7;
    run.side = "white";
    run.title = "Anderssen – Kieseritzky, 1851";
    run.started = run.updated = 100;
    run.plies = 45;
    auto id = storage::saveGuessRun(*db, run);
    REQUIRE(id.ok());
    run.id = *id;
    run.ply = 4;
    run.first = 1;
    run.second = 1;
    REQUIRE(storage::saveGuessRun(*db, run).ok());
    REQUIRE(storage::addGuessMove(*db, run.id, 0, 0, "", false).ok());
    REQUIRE(storage::addGuessMove(*db, run.id, 2, 1, "d2d4", true).ok());
    auto back = storage::guessRun(*db, run.id);
    REQUIRE(back.ok());
    REQUIRE(back->has_value());
    CHECK_EQ((*back)->ply, 4);
    CHECK_EQ((*back)->title, run.title);
    CHECK_FALSE((*back)->finished);
    auto st = storage::guessStats(*db);
    REQUIRE(st.ok());
    CHECK_EQ(st->runs, 1);
    CHECK_EQ(st->finished, 0);
    CHECK_EQ(st->percent(), 75);
    auto none = storage::guessRun(*db, 999);
    REQUIRE(none.ok());
    CHECK_FALSE(none->has_value());
}

TEST_CASE("guess screen: the Immortal as White, replies, the score in chess.db, resume and study") {
    const auto root = classicsRoot("pchess-guess");
    {
        GuessHarness h(root);
        const int64_t id = h.immortal();
        GuessScreen::choose(*h.app, id);
        h.app->flush();
        h.run("key:ok");  // "Play White (Anderssen…)"
        GuessScreen* g = h.guess();
        REQUIRE(g != nullptr);
        REQUIRE(g->loaded());
        CHECK(g->session()->userToMove());
        CHECK(g->status().rfind("Your move", 0) == 0);
        REQUIRE(g->runId() != 0);

        CHECK(g->playUci("e2e4"));
        CHECK(g->status().find("first try") != std::string::npos);
        CHECK_FALSE(g->playUci("f2f4"));  // the reply is still pending
        h.pump(600ms);
        CHECK_EQ(g->session()->ply(), 2);
        CHECK(g->info().find("replied 1… e5") != std::string::npos);

        CHECK(g->playUci("g1f3"));  // 2. f4 in the game
        CHECK(g->status().find("not the game move") != std::string::npos);
        h.run("key:prev");  // hint
        CHECK(g->session()->hinted());
        CHECK(g->playUci("f2f4"));
        h.pump(600ms);
        h.run("key:next");  // show 3. Bc4
        CHECK(g->status().find("Game move: 3. Bc4") != std::string::npos);
        h.pump(600ms);
        CHECK_EQ(g->session()->ply(), 6);
        h.app->flush();
        test::checkGolden("guess", h.app->canvas());

        storage::Database* db = h.app->userDb();
        REQUIRE(db != nullptr);
        auto run = storage::guessRun(*db, g->runId());
        REQUIRE(run.ok());
        REQUIRE(run->has_value());
        CHECK_EQ((*run)->first, 1);
        CHECK_EQ((*run)->second, 1);
        CHECK_EQ((*run)->missed, 1);
        CHECK_EQ((*run)->ply, 5);  // saved after the user's move; the reply is replayed on resume
        CHECK_EQ((*run)->side, std::string("white"));
        auto moves = db->prepare("SELECT ply, outcome, wrong, hinted FROM guess_moves ORDER BY ply");
        REQUIRE(moves.ok());
        std::string log;
        while (*moves->step())
            log += std::to_string(moves->integer(0)) + ":" + std::to_string(moves->integer(1)) + ":" + moves->text(2) +
                   ":" + std::to_string(moves->integer(3)) + " ";
        CHECK_EQ(log, std::string("0:0::0 2:1:g1f3:1 4:2::0 "));
    }
    {
        // A new start resumes the run where it stopped (session), with its score.
        GuessHarness h(root);
        auto session = storage::loadSession(*h.app->userDb());
        REQUIRE(session.ok());
        CHECK_EQ((*session)["screen"], std::string("guess"));
        auto g = GuessScreen::restore(*h.app, *session);
        REQUIRE(g != nullptr);
        CHECK_EQ(g->session()->ply(), 6);
        CHECK(g->session()->userToMove());
        CHECK_EQ(g->session()->score().guessed(), 3);
        h.app->push(std::move(g));
        h.app->flush();

        // Menu → "Study this game": the study view at the same position.
        h.run("key:menu;key:ok");
        auto* s = dynamic_cast<GameStudyScreen*>(h.app->top());
        REQUIRE(s != nullptr);
        CHECK_EQ(s->node()->ply, 6);
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("guess screen: a whole game shown move by move finishes and counts in the totals") {
    const auto root = classicsRoot("pchess-guess-finish");
    GuessHarness h(root);
    const int64_t id = h.immortal();
    h.app->push(std::make_unique<GuessScreen>(*h.app, id, GuessScreen::Side::Both));
    h.app->flush();
    GuessScreen* g = h.guess();
    REQUIRE(g != nullptr);
    const int plies = g->session()->plies();
    for (int i = 0; i < plies; ++i) g->reveal();
    CHECK(g->session()->finished());
    CHECK(g->status().rfind("Game over", 0) == 0);
    auto st = storage::guessStats(*h.app->userDb());
    REQUIRE(st.ok());
    CHECK_EQ(st->finished, 1);
    CHECK_EQ(st->missed, plies);
    std::map<std::string, std::string> out;
    CHECK_FALSE(g->saveSession(out));  // a finished run is not resumed
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
