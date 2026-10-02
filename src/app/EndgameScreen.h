// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/engine/EngineManager.h"
#include "core/games/Endgames.h"
#include "core/storage/ChessDb.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pchess::app {

// Endgame practice (M7, ADR 0018): a known position played out against the engine at full strength
// until the goal is reached (mate, a won promotion, or a draw held) or lost. ◀ takes back the last
// move pair, ▶ asks the engine for a hint; both are recorded. Attempts go to chess.db.
class EndgameScreen final : public Screen {
public:
    // `moves`/`attemptId`: a resumed attempt (session).
    EndgameScreen(Application& app, games::EndgameTask task, std::vector<chess::Move> moves = {}, int64_t attemptId = 0);
    ~EndgameScreen() override;
    const char* name() const override { return "endgame"; }
    std::string title() const override { return "Endgame"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onEnter() override;
    void onLifecycle(platform::Lifecycle l) override;
    bool saveSession(std::map<std::string, std::string>& out) const override;
    static std::unique_ptr<EndgameScreen> restore(Application& app, const std::map<std::string, std::string>& session);

    // The task list (Puzzles → Endgames): titles with their records, then the screen.
    static void choose(Application& app);
    static std::vector<games::EndgameTask> loadTasks(Application& app);

    // For tests and scripted sessions.
    const chess::Position& position() const { return pos_; }
    bool userToMove() const;
    bool thinking() const { return job_ != 0; }
    bool over() const { return result_ != games::EndgameJudge::Verdict::Continue; }
    games::EndgameJudge::Verdict result() const { return result_; }
    const std::string& status() const { return status_; }
    int64_t attemptId() const { return attempt_.id; }
    bool playUci(const std::string& uci);
    void takeBack();
    void hint();

private:
    enum ButtonId { BtnBack, BtnHint, BtnMenu };  // ◀ ▶ = the Prev/Next keys

    void layoutPanel();
    void replay();  // pos_ and keys_ from start + moves_
    void afterMove(std::optional<int> engineScore);
    void engineMove();
    void finish(const games::EndgameJudge::Result& r);
    void save();
    void setStatus(std::string status, std::string info);
    void refreshBoard(ui::RefreshHint hint);
    void updateButtons();
    void onSquare(chess::Square s);
    void select(chess::Square s);
    void tryMove(chess::Square from, chess::Square to);
    void commit(chess::Move m);
    void openMenu();
    void analyzeHere();
    void cancelJob();

    games::EndgameTask task_;
    games::EndgameJudge judge_;
    chess::Position start_, pos_;
    std::vector<chess::Move> moves_;
    std::vector<uint64_t> keys_;  // positions before the current one
    bool promoted_ = false;       // the user has promoted a pawn
    games::EndgameJudge::Verdict result_ = games::EndgameJudge::Verdict::Continue;
    storage::EndgameAttempt attempt_;
    engine::JobId job_ = 0;
    chess::Square selected_ = chess::NoSquare;
    chess::Square hintSquare_ = chess::NoSquare;
    bool flipped_ = false;
    std::string status_, info_;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, infoRect_, buttonsRect_, backRect_;
    std::shared_ptr<char> alive_ = std::make_shared<char>('\0');
};

} // namespace pchess::app
