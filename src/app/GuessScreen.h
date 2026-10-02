// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/chess/Pgn.h"
#include "core/games/GamePack.h"
#include "core/games/GuessSession.h"
#include "core/util/Scheduler.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pchess::app {

// Guess the move (M7): play through a game of the current pack as one side (or both), guessing each
// move the player made. ◀ = hint (the piece), ▶ = show the move; the other side replies by itself.
// Every guessed move and the run's score go to user/chess.db (guess_runs, guess_moves).
class GuessScreen final : public Screen {
public:
    using Side = games::GuessSession::Side;

    // A new run from main-line ply `fromPly`, or (runId != 0) a saved run resumed where it stopped.
    GuessScreen(Application& app, int64_t gameId, Side side, int fromPly = 0, int64_t runId = 0);
    ~GuessScreen() override;
    const char* name() const override { return "guess"; }
    std::string title() const override { return "Guess"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    bool saveSession(std::map<std::string, std::string>& out) const override;
    static std::unique_ptr<GuessScreen> restore(Application& app, const std::map<std::string, std::string>& session);

    // A random game id of the current pack matching `filter` (0 when the pack has none).
    static int64_t randomGame(Application& app, const games::GameFilter& filter);
    // The side chooser (White / Black / Both) for a game, then the run; used by the Games screens.
    static void choose(Application& app, int64_t gameId, int fromPly = 0, games::GameFilter another = {});

    // For tests and scripted sessions.
    bool loaded() const { return session_.has_value(); }
    const games::GuessSession* session() const { return session_ ? &*session_ : nullptr; }
    int64_t runId() const { return runId_; }
    const std::string& status() const { return status_; }
    const std::string& info() const { return info_; }
    bool playUci(const std::string& uci);
    void hint();
    void reveal();

private:
    enum ButtonId { BtnHint, BtnShow, BtnMenu };  // ◀ ▶ = the Prev/Next keys

    void layoutPanel();
    void afterUserMove(games::GuessSession::Verdict v, const std::string& san);
    void reply();
    void yourTurn(std::string status, std::string info);
    void finish();
    void persist(int ply, games::GuessSession::Outcome o, bool hinted);
    void refreshBoard(ui::RefreshHint hint);
    void updateButtons();
    void setStatus(std::string status, std::string info);
    void onSquare(chess::Square s);
    void select(chess::Square s);
    void tryMove(chess::Square from, chess::Square to);
    void commit(chess::Move m);
    void openMenu();
    void study();
    void another();
    std::string player(chess::Color c) const;
    std::string moveText(const chess::Position& before, chess::Move m) const;  // "12. Nf3" / "12… Bd7"
    std::string commentAt(int ply) const;  // the game's comment after main-line move `ply`
    std::string scoreText() const;

    int64_t gameId_ = 0;
    std::string pack_;
    chess::PgnGame game_;
    games::GameRow row_;
    std::vector<chess::Move> line_;
    std::optional<games::GuessSession> session_;
    games::GameFilter another_;  // "Another game" picks from this filter
    int64_t runId_ = 0;
    int64_t started_ = 0;
    std::string firstWrong_;  // UCI of the current move's first wrong try
    chess::Square selected_ = chess::NoSquare;
    chess::Square hintSquare_ = chess::NoSquare;
    bool flipped_ = false;
    std::string status_, info_, error_;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, infoRect_, scoreRect_, buttonsRect_, backRect_;
    util::Timer timer_;
    std::shared_ptr<char> alive_ = std::make_shared<char>('\0');
};

} // namespace pchess::app
