// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/board/BoardView.h"

namespace pchess::ui {

using namespace chess;

BoardVisual BoardVisual::fromPosition(const Position& pos, Move lastMove) {
    BoardVisual v;
    for (Square s = 0; s < 64; ++s) v.pieces[static_cast<size_t>(s)] = pos.pieceAt(s);
    if (!lastMove.isNone()) {
        v.lastFrom = lastMove.from();
        v.lastTo = lastMove.to();
    }
    if (pos.inCheck()) v.check = pos.kingSquare(pos.sideToMove());
    return v;
}

namespace {

// Compact per-square appearance key.
uint32_t squareKey(const BoardVisual& v, Square s) {
    uint32_t k = v.pieces[static_cast<size_t>(s)];
    if (v.selected == s) k |= 1u << 8;
    if (v.targets & bb(s)) k |= 1u << 9;
    if (v.lastFrom == s || v.lastTo == s) k |= 1u << 10;
    if (v.check == s) k |= 1u << 11;
    return k;
}

} // namespace

std::vector<Square> affectedSquares(const BoardVisual& a, const BoardVisual& b) {
    std::vector<Square> out;
    for (Square s = 0; s < 64; ++s)
        if (a.flipped != b.flipped || squareKey(a, s) != squareKey(b, s)) out.push_back(s);
    return out;
}

void BoardView::setRect(const Rect& r) {
    rect_ = r;
    sq_ = r.w / 8;
}

Rect BoardView::squareRect(Square s) const {
    const int file = visual_.flipped ? 7 - fileOf(s) : fileOf(s);
    const int rank = visual_.flipped ? rankOf(s) : 7 - rankOf(s);
    return {rect_.x + file * sq_, rect_.y + rank * sq_, sq_, sq_};
}

Square BoardView::squareAt(Point p) const {
    if (sq_ <= 0 || !Rect{rect_.x, rect_.y, sq_ * 8, sq_ * 8}.contains(p)) return NoSquare;
    int file = (p.x - rect_.x) / sq_;
    int row = (p.y - rect_.y) / sq_;
    if (visual_.flipped) return makeSquare(7 - file, row);
    return makeSquare(file, 7 - row);
}

std::vector<Rect> BoardView::update(const BoardVisual& v) {
    std::vector<Rect> rects;
    const BoardVisual old = visual_;
    visual_ = v;
    if (old.flipped != v.flipped) {
        rects.push_back({rect_.x, rect_.y, sq_ * 8, sq_ * 8});
        return rects;
    }
    for (Square s : affectedSquares(old, v)) rects.push_back(squareRect(s));
    return rects;
}

void BoardView::draw(Canvas& canvas, const Rect& clip, const BoardTheme& theme, PieceCache& pieces,
                     FontLibrary& fonts) const {
    for (Square s = 0; s < 64; ++s) {
        const Rect r = squareRect(s);
        if (!r.intersects(clip)) continue;
        canvas.pushClip(clip.intersected(r));
        drawSquare(canvas, s, theme, pieces, fonts);
        canvas.popClip();
    }
}

void BoardView::drawSquare(Canvas& c, Square s, const BoardTheme& t, PieceCache& pieces, FontLibrary& fonts) const {
    const Rect r = squareRect(s);
    const bool light = ((fileOf(s) + rankOf(s)) & 1) != 0;
    const Rgb base = light ? t.light : t.dark;
    const bool isSel = visual_.selected == s;
    const bool isLast = visual_.lastFrom == s || visual_.lastTo == s;
    const bool isCheck = visual_.check == s;

    Rgb fill = base;
    if (t.tintMarkers) {
        if (isLast) fill = blend(base, t.lastMove, 200);
        if (isSel) fill = t.selected;
        if (isCheck) fill = t.check;
    }
    c.fill(r, fill);

    const int stroke = std::max(2, sq_ / 22);
    auto drawStyle = [&](MarkerStyle style, Rgb color) {
        switch (style) {
        case MarkerStyle::Tint: break;
        case MarkerStyle::Outline: c.frame(r, stroke * 2, color); break;
        case MarkerStyle::Hatch:
            c.hatch(r, std::max(8, sq_ / 9), std::max(2, sq_ / 40), light ? blend(t.light, color, 110) : blend(t.dark, color, 110));
            c.frame(r, stroke, color);
            break;
        case MarkerStyle::Dots: c.dottedFrame(r.inset(stroke), stroke, std::max(4, sq_ / 16), color); break;
        case MarkerStyle::DoubleBorder:
            c.frame(r, stroke, color);
            c.frame(r.inset(stroke * 2), stroke, color);
            break;
        }
    };
    const Rgb markInk = t.mono ? t.selected : t.frame;
    if (isLast) drawStyle(t.lastMoveStyle, t.mono ? t.lastMove : blend(fill, t.frame, 150));
    if (isCheck) drawStyle(t.checkStyle, t.mono ? t.check : blend(t.check, t.frame, 150));
    if (isSel) drawStyle(t.selectedStyle, markInk);

    // Coordinates inside the edge squares (no margins; architecture doc §3).
    if (t.coordinates && fonts.loaded()) {
        Font& f = fonts.get(std::max(12, sq_ / 6), FontStyle::Bold);
        const Rgb ink = light ? t.coordOnLight : t.coordOnDark;
        const int pad = std::max(3, sq_ / 30);
        const bool bottomRow = visual_.flipped ? rankOf(s) == 7 : rankOf(s) == 0;
        const bool leftCol = visual_.flipped ? fileOf(s) == 7 : fileOf(s) == 0;
        if (bottomRow) {
            const char file[2] = {static_cast<char>('a' + fileOf(s)), 0};
            f.draw(c, r.right() - pad - f.measure(file), r.bottom() - pad - f.descent() / 2, file, ink);
        }
        if (leftCol) {
            const char rank[2] = {static_cast<char>('1' + rankOf(s)), 0};
            f.draw(c, r.x + pad, r.y + pad + f.ascent() - f.ascent() / 4, rank, ink);
        }
    }

    const Piece p = visual_.pieces[static_cast<size_t>(s)];
    if (p != NoPiece) c.blit(pieces.get(p, sq_), r.x, r.y);

    if (visual_.targets & bb(s)) {
        const Point ctr = r.center();
        if (p != NoPiece) {
            c.ring(ctr.x, ctr.y, sq_ / 2 - stroke, sq_ / 2 - stroke - std::max(4, sq_ / 14), t.marker);
        } else {
            c.fillCircle(ctr.x, ctr.y, std::max(6, sq_ / 7), t.marker);
            if (t.mono) c.fillCircle(ctr.x, ctr.y, std::max(2, sq_ / 7 - stroke * 2), light ? t.light : t.dark);
        }
    }
}

} // namespace pchess::ui
