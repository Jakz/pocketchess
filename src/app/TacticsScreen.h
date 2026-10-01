// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/puzzles/PuzzleSession.h"
#include "core/util/Scheduler.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace pchess::app {

// Tactics trainer and Review (M4, design notes §9–11, architecture doc §10). Train picks unseen
// puzzles near the user's rating (PuzzleSelector) and rates the first attempt; Review replays the
// due failures from progress.db and only reschedules them. The engine is only used on request
// ("Check with engine").
class TacticsScreen final : public Screen {
public:
    enum class Mode { Train, Review };

    TacticsScreen(Application& app, Mode mode, const std::string& puzzleId = "");
    ~TacticsScreen() override;
    const char* name() const override { return "tactics"; }
    std::string title() const override { return mode_ == Mode::Review ? "Review" : "Puzzles"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    bool saveSession(std::map<std::string, std::string>& out) const override;
    static std::unique_ptr<TacticsScreen> restore(Application& app, const std::map<std::string, std::string>& session);

    // For tests and scripted sessions.
    Mode mode() const { return mode_; }
    const puzzles::PuzzleSession* session() const { return session_ ? &*session_ : nullptr; }
    const std::string& status() const { return status_; }
    const std::string& info() const { return info_; }
    bool playUci(const std::string& uci);  // the user's move
    void next();                           // next puzzle, or the opponent's move when waiting
    void hint();
    void retry();
    void showSolution();
    int reviewsLeft() const { return static_cast<int>(queue_.size()); }
    const std::vector<ui::Button>& buttons() const { return buttons_; }

private:
    enum ButtonId { BtnRetry, BtnNext, BtnHint, BtnSolution, BtnMenu };  // ◀ ▶ = the Prev/Next keys

    void layoutPanel();
    bool load(const std::string& id);  // a specific puzzle
    void loadNext();
    void startSession(puzzles::Puzzle p);
    void afterOpponent();
    void onVerdict(puzzles::PuzzleSession::Verdict v);
    void finish();
    void stepSolution();
    void refreshBoard(ui::RefreshHint hint);
    void refreshPanel();
    void updateButtons();
    void setStatus(std::string status, std::string info);
    void onSquare(chess::Square s);
    void select(chess::Square s);
    void tryMove(chess::Square from, chess::Square to);
    void commit(chess::Move m);
    void openMenu();
    void analyzePosition();
    void checkWithEngine();
    std::string sideText() const;

    Mode mode_;
    std::optional<puzzles::PuzzleSession> session_;
    std::unordered_set<std::string> seen_;
    std::vector<std::string> queue_;  // Review: due puzzle ids still to do
    chess::Move lastWrong_;
    chess::Square selected_ = chess::NoSquare;
    chess::Square hintSquare_ = chess::NoSquare;
    bool flipped_ = false;
    bool recorded_ = false;
    std::string status_, info_;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, infoRect_, buttonsRect_, backRect_;
    util::Timer timer_;
    std::shared_ptr<char> alive_ = std::make_shared<char>('\0');
};

} // namespace pchess::app
