// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/games/OpeningDrill.h"
#include "core/util/Scheduler.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pchess::app {

// Opening practice (M7, ADR 0016): from the standard start or a named line, the opponent plays
// the moves the largest game pack's players chose (weighted), and the user must find a book move.
// ◀ = hint, ▶ = show the most played move. Every move goes to user/chess.db (drill_runs, drill_moves).
class DrillScreen final : public Screen {
public:
    // `start`: moves from the standard start (a named line); `played`/`runId`: a resumed drill.
    DrillScreen(Application& app, std::vector<chess::Move> start, chess::Color user,
                std::vector<chess::Move> played = {}, int64_t runId = 0);
    ~DrillScreen() override;
    const char* name() const override { return "drill"; }
    std::string title() const override { return "Practice"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    bool saveSession(std::map<std::string, std::string>& out) const override;
    static std::unique_ptr<DrillScreen> restore(Application& app, const std::map<std::string, std::string>& session);

    // The side chooser, then the drill (Openings screen). `name`: the line's name for the title.
    static void choose(Application& app, std::vector<chess::Move> start, const std::string& name);

    // For tests and scripted sessions.
    bool ready() const { return drill_.has_value(); }
    const games::OpeningDrill* drill() const { return drill_ ? &*drill_ : nullptr; }
    int64_t runId() const { return runId_; }
    const std::string& status() const { return status_; }
    const std::string& info() const { return info_; }
    bool playUci(const std::string& uci);
    void hint();
    void reveal();

private:
    enum ButtonId { BtnHint, BtnShow, BtnMenu };  // ◀ ▶ = the Prev/Next keys

    void layoutPanel();
    void afterUserMove(games::OpeningDrill::Verdict v, const chess::Position& before, chess::Move m);
    void reply();
    void yourTurn(std::string status, std::string info);
    void finish();
    void persist(const chess::Position& before, chess::Move m, bool hinted);
    void saveRun();
    void refreshBoard(ui::RefreshHint hint);
    void updateButtons();
    void setStatus(std::string status, std::string info);
    void onSquare(chess::Square s);
    void select(chess::Square s);
    void tryMove(chess::Square from, chess::Square to);
    void commit(chess::Move m);
    void openMenu();
    void analyzeHere();
    std::string moveText(const chess::Position& before, chess::Move m) const;  // "6. Bg5" / "5… a6"
    std::string openingName() const;
    std::string scoreText() const;

    std::vector<chess::Move> start_;
    chess::Color user_;
    std::optional<games::OpeningDrill> drill_;
    std::string packName_, packTitle_;
    int64_t runId_ = 0;
    int64_t started_ = 0;
    std::string firstWrong_;
    chess::Square selected_ = chess::NoSquare;
    chess::Square hintSquare_ = chess::NoSquare;
    bool flipped_ = false;
    bool recorded_ = false;  // the finished run is saved
    std::string status_, info_;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, infoRect_, scoreRect_, buttonsRect_, backRect_;
    util::Timer timer_;
    std::shared_ptr<char> alive_ = std::make_shared<char>('\0');
};

} // namespace pchess::app
