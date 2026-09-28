// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Bitboard.h"
#include "core/chess/Position.h"
#include "ui/canvas/Canvas.h"
#include "ui/canvas/Font.h"
#include "ui/theme/BoardTheme.h"
#include "ui/theme/PieceSet.h"

#include <array>
#include <vector>

namespace pchess::ui {

// Everything that decides how the 64 squares look. Two states are compared square by square to
// find the minimal set of squares to redraw (design notes §7.3).
struct BoardVisual {
    std::array<chess::Piece, 64> pieces{};
    chess::Square selected = chess::NoSquare;
    chess::Bitboard targets = 0;  // legal destinations of the selected piece
    chess::Square lastFrom = chess::NoSquare;
    chess::Square lastTo = chess::NoSquare;
    chess::Square check = chess::NoSquare;
    bool flipped = false;

    BoardVisual() { pieces.fill(chess::NoPiece); }
    static BoardVisual fromPosition(const chess::Position& pos, chess::Move lastMove = chess::Move::none());
};

// Squares whose appearance differs between a and b (all 64 when the orientation changes).
std::vector<chess::Square> affectedSquares(const BoardVisual& a, const BoardVisual& b);

class BoardView {
public:
    // Board rect; the square size is rect.w / 8 (the rect should be 8 * square wide and high).
    void setRect(const Rect& r);
    const Rect& rect() const { return rect_; }
    int squareSize() const { return sq_; }

    Rect squareRect(chess::Square s) const;
    chess::Square squareAt(Point p) const;  // NoSquare outside the board

    const BoardVisual& visual() const { return visual_; }
    // Replaces the visual state; returns the screen rects that must be redrawn.
    std::vector<Rect> update(const BoardVisual& v);

    void draw(Canvas& canvas, const Rect& clip, const BoardTheme& theme, PieceCache& pieces, FontLibrary& fonts) const;

private:
    void drawSquare(Canvas& canvas, chess::Square s, const BoardTheme& theme, PieceCache& pieces, FontLibrary& fonts) const;

    Rect rect_;
    int sq_ = 0;
    BoardVisual visual_;
};

} // namespace pchess::ui
