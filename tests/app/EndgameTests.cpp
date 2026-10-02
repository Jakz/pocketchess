// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/EndgameScreen.h"
#include "app/HeadlessHost.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/games/Endgames.h"
#include "core/storage/ChessDb.h"
#include "core/util/Files.h"
#include "support/FakeEngine.h"
#include "support/Golden.h"
#include "support/Test.h"

#include <algorithm>
#include <filesystem>

using namespace pchess;
using namespace pchess::app;
using namespace std::chrono_literals;
using games::EndgameJudge;
using games::EndgameTask;

namespace {

std::vector<EndgameTask> shippedTasks(int* rejected = nullptr) {
    const auto file = std::filesystem::path(test::fontDir()).parent_path() / "endgames" / "endgames.tsv";
    auto text = util::readTextFile(file);
    REQUIRE(text.ok());
    return games::parseEndgames(*text, rejected);
}

EndgameTask task(const char* fen, EndgameTask::Goal goal, chess::Color side = chess::White, int hold = 0) {
    EndgameTask t;
    t.id = "test";
    t.title = "Test";
    t.goal = goal;
    t.side = side;
    t.holdMoves = hold;
    t.fen = fen;
    t.advice = "Advice.";
    return t;
}

EndgameJudge::Verdict after(const EndgameTask& t, const char* uci, std::optional<int> engineCp = std::nullopt,
                            int userMoves = 1) {
    chess::Position p = chess::parseFen(t.fen).value();
    std::vector<uint64_t> keys{p.key()};
    p.makeMove(chess::parseUci(p, uci).value());
    return EndgameJudge(t).judge(p, keys, engineCp, false, userMoves).verdict;
}

} // namespace

TEST_CASE("endgames: the shipped tasks parse, and bad lines are rejected") {
    int rejected = -1;
    const auto tasks = shippedTasks(&rejected);
    CHECK_EQ(rejected, 0);
    REQUIRE_EQ(tasks.size(), size_t(10));
    for (const auto& t : tasks) {
        CHECK_FALSE(t.title.empty());
        CHECK_FALSE(t.advice.empty());
        CHECK(chess::parseFen(t.fen).ok());
        if (t.goal == EndgameTask::Goal::Draw) CHECK(t.holdMoves > 0);
    }
    int bad = 0;
    CHECK(games::parseEndgames("x\tC\tT\twin\tgreen\t0\t8/8/8/8/8/8/8/K6k w - - 0 1\ta\n"
                               "y\tC\tT\tmate\twhite\t0\tnot a fen\ta\n# comment\n",
                               &bad)
              .empty());
    CHECK_EQ(bad, 2);
}

TEST_CASE("endgames: the judge - mate, stalemate, a slipped win, draws held or lost") {
    const auto mate = task("k7/8/1K6/8/8/8/7Q/8 w - - 0 1", EndgameTask::Goal::Mate);
    CHECK(after(mate, "h2h8") == EndgameJudge::Verdict::Solved);     // Qh8#
    CHECK(after(mate, "h2h3", -900) == EndgameJudge::Verdict::Continue);
    CHECK(after(mate, "h2h3", 0) == EndgameJudge::Verdict::Failed);  // the engine sees no win any more
    const auto stale = task("k7/8/1K6/8/8/8/8/2Q5 w - - 0 1", EndgameTask::Goal::Mate);
    CHECK(after(stale, "c1c7") == EndgameJudge::Verdict::Failed);    // stalemate

    const auto hold = task("4k3/8/8/4P3/4K3/8/8/8 b - - 0 1", EndgameTask::Goal::Draw, chess::Black, 15);
    CHECK(after(hold, "e8e7", 0, 1) == EndgameJudge::Verdict::Continue);
    CHECK(after(hold, "e8e7", 0, 15) == EndgameJudge::Verdict::Solved);
    CHECK(after(hold, "e8d8", 600, 1) == EndgameJudge::Verdict::Failed);
    CHECK(after(task("8/8/8/3k4/4P3/8/8/K7 b - - 0 1", EndgameTask::Goal::Draw, chess::Black, 15), "d5e4") ==
          EndgameJudge::Verdict::Solved);  // the pawn taken: insufficient material
}

TEST_CASE("endgame screen: engine replies, take back, hint, a mate solved and recorded") {
    const auto root = std::filesystem::temp_directory_path() / "pchess-endgame";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    HeadlessHost host;
    HeadlessPresenter presenter{platform::DeviceProfile::verseProColor()};
    test::FakeEngineFactory fake;
    fake.behaviour.evalCp = [](const chess::Position&) { return -900; };  // the defender is lost
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
    auto pump = [&] {
        for (int i = 0; i < 100; ++i) {
            host.advance(20ms);
            app->flush();
        }
    };

    // The Lucena position from the shipped file: one move, the engine's reply, a take-back, a hint.
    const auto tasks = shippedTasks();
    const auto lucena = std::find_if(tasks.begin(), tasks.end(), [](const EndgameTask& t) { return t.id == "rook-lucena"; });
    REQUIRE(lucena != tasks.end());
    app->push(std::make_unique<EndgameScreen>(*app, *lucena));
    app->flush();
    auto* e = dynamic_cast<EndgameScreen*>(app->top());
    REQUIRE(e != nullptr);
    REQUIRE(e->userToMove());
    CHECK(e->playUci("c1d1"));
    CHECK(e->thinking());
    pump();
    REQUIRE(e->userToMove());
    test::checkGolden("endgame", app->canvas());
    e->takeBack();
    CHECK_EQ(chess::toFen(e->position()), lucena->fen);
    e->hint();
    pump();
    CHECK(e->status().rfind("Hint: ", 0) == 0);

    // A mate in one: solved, and the record says so.
    app->pop();
    app->flush();
    app->push(std::make_unique<EndgameScreen>(*app, task("k7/8/1K6/8/8/8/7Q/8 w - - 0 1", EndgameTask::Goal::Mate)));
    app->flush();
    e = dynamic_cast<EndgameScreen*>(app->top());
    REQUIRE(e != nullptr);
    CHECK(e->playUci("h2h8"));
    CHECK(e->over());
    CHECK(e->result() == EndgameJudge::Verdict::Solved);
    auto records = storage::endgameRecords(*app->userDb());
    REQUIRE(records.ok());
    CHECK_EQ((*records)["test"].solved, 1);
    CHECK_EQ((*records)["test"].solvedAlone, 1);
    CHECK_EQ((*records)["test"].fewestMoves, 1);
    CHECK_EQ((*records)["rook-lucena"].attempts, 0);  // unfinished attempts are not counted
    app.reset();
    std::filesystem::remove_all(root, ec);
}
