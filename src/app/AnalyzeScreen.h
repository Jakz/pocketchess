// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Screen.h"
#include "core/chess/GameTree.h"
#include "core/chess/Move.h"
#include "core/chess/Position.h"
#include "ui/board/BoardView.h"
#include "ui/widgets/Widgets.h"

#include <string>
#include <vector>

namespace pchess::app {

// "Analyze board" sandbox (M1): make moves for both sides, go back/forward through the game
// tree (moves from the middle create variations), flip, FEN import/export, new game.
// Hardware Prev/Next = back/forward; Menu = action menu. The engine panel arrives in M2.
class AnalyzeScreen final : public Screen {
public:
    AnalyzeScreen(Application& app, const std::string& fen);
    const char* name() const override { return "analyze"; }
    void draw(ui::Canvas& c, const ui::Rect& clip) override;
    void onGesture(const ui::Gesture& g) override;
    void onKey(const ui::InputEvent& e) override;

    // For tests and scripted sessions.
    const chess::Position& position() const { return pos_; }
    const chess::GameTree& tree() const { return tree_; }
    bool playSan(const std::string& san);
    void select(chess::Square s);
    void goBack();
    void goForward();
    void flip();
    bool loadFen(const std::string& fen);

private:
    enum ButtonId { BtnBack, BtnForward, BtnFlip, BtnFen, BtnNew };

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

    chess::GameTree tree_;
    chess::GameNode* node_ = nullptr;
    chess::Position pos_;
    chess::MoveList legal_;
    chess::Square selected_ = chess::NoSquare;
    bool flipped_ = false;

    ui::BoardView board_;
    std::vector<ui::Button> buttons_;
    ui::Rect statusRect_, movesRect_, buttonsRect_;
};

} // namespace pchess::app
