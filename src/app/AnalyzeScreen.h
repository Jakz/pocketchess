// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/chess/GameTree.h"
#include "core/chess/Move.h"
#include "core/chess/Position.h"
#include "core/engine/EngineManager.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace pchess::app {

// "Analyze board" sandbox (M1): make moves for both sides, go back/forward through the game
// tree (moves from the middle create variations), flip, FEN import/export, new game.
// Hardware Prev/Next = back/forward; Menu = action menu.
// Engine panel (M2, design notes §8): Off / Quick / Deep. Quick re-analyses every position with
// the profile's short budget; Deep runs until the profile's auto-stop. Evaluation, depth, best
// move and PV (SAN) refresh at most once per second and only when their text changed.
class AnalyzeScreen final : public Screen {
public:
    enum class EngineMode { Off, Quick, Deep };

    AnalyzeScreen(Application& app, const std::string& fen);
    ~AnalyzeScreen() override;
    const char* name() const override { return "analyze"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;
    void onEnter() override;
    void onLeave() override;
    void onLifecycle(platform::Lifecycle l) override;

    // For tests and scripted sessions.
    const chess::Position& position() const { return pos_; }
    const chess::GameTree& tree() const { return tree_; }
    bool playSan(const std::string& san);
    void select(chess::Square s);
    void goBack();
    void goForward();
    void flip();
    bool loadFen(const std::string& fen);
    void setEngineMode(EngineMode m);
    void cycleEngineMode();
    EngineMode engineMode() const { return engineMode_; }
    // The engine lines as drawn (after throttling): evaluation/best move, and the PV.
    const std::string& engineHeadline() const { return shown_.head; }
    const std::string& enginePv() const { return shown_.pv; }
    bool engineJobRunning() const { return job_ != 0; }
    const std::vector<ui::Button>& buttons() const { return buttons_; }

private:
    enum ButtonId { BtnBack, BtnForward, BtnFlip, BtnEngine, BtnMenu };
    struct EngineText {
        std::string head, pv;
        bool operator==(const EngineText&) const = default;
    };

    void layoutPanel();
    void onSquare(chess::Square s);
    void tryMove(chess::Square from, chess::Square to);
    void commitMove(chess::Move m);
    void setNode(chess::GameNode* node);
    void refreshBoard(ui::RefreshHint hint);
    void refreshPanel();
    void updateButtons();
    void openMenu();
    void editFen();
    void reset(const chess::Position& start);
    std::string statusText() const;
    void drawMoves(ui::Canvas& c);
    void drawEngine(ui::Canvas& c);

    void startAnalysis();
    void stopAnalysis();
    void onEngineInfo(engine::JobId id, const engine::SearchInfo& info);
    void onEngineDone(const engine::JobResult& r);
    EngineText formatInfo(const engine::SearchInfo& info, bool final) const;
    void updateEngineText(EngineText t, bool now);
    void showEngineMissing(const std::string& detail);

    chess::GameTree tree_;
    chess::GameNode* node_ = nullptr;
    chess::Position pos_;
    chess::MoveList legal_;
    chess::Square selected_ = chess::NoSquare;
    bool flipped_ = false;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, engineRect_, movesRect_, buttonsRect_;

    EngineMode engineMode_ = EngineMode::Off;
    engine::JobId job_ = 0;
    EngineText shown_, pending_;
    std::chrono::steady_clock::time_point lastEngineDraw_{};
    platform::TimerId throttleTimer_ = 0;
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);
};

} // namespace pchess::app
