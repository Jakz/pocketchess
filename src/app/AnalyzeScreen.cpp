// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/Rules.h"
#include "core/chess/San.h"
#include "core/util/Log.h"

#include <algorithm>

namespace pchess::app {

using namespace ui;
using namespace chess;

AnalyzeScreen::AnalyzeScreen(Application& app, const std::string& fen) : Screen(app) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    Position start = Position::startPosition();
    if (!fen.empty()) {
        auto p = parseFen(fen);
        if (p) start = p.value();
        else util::Log::warn("analyze", p.error().message());
    }
    reset(start);
}

void AnalyzeScreen::layoutPanel() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect p = l.panel;
    const int btnH = std::min(kMinTouchPx, p.h / 3);
    const int statusH = app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() + pad;
    statusRect_ = {p.x + pad * 2, p.y + pad, p.w - pad * 4, statusH};
    buttonsRect_ = {p.x + pad, p.bottom() - pad - btnH, p.w - 2 * pad, btnH};
    movesRect_ = {p.x + pad * 2, statusRect_.bottom(), p.w - pad * 4, buttonsRect_.y - pad - statusRect_.bottom()};

    const char* labels[] = {"◀ Back", "Next ▶", "Flip", "FEN", "New"};
    const auto cells = splitRow(buttonsRect_, 5, pad);
    buttons_.clear();
    for (int i = 0; i < 5; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        b.rect = cells[static_cast<size_t>(i)];
        buttons_.push_back(b);
    }
}

void AnalyzeScreen::reset(const Position& start) {
    tree_ = GameTree(start);
    selected_ = NoSquare;
    setNode(tree_.root());
    app_.invalidate(app_.layout().panel, RefreshHint::Content);
}

void AnalyzeScreen::setNode(GameNode* node) {
    node_ = node;
    pos_ = tree_.positionAt(node_);
    generateLegalMoves(pos_, legal_);
    selected_ = NoSquare;
    refreshBoard(RefreshHint::Content);
    refreshPanel();
}

void AnalyzeScreen::refreshBoard(RefreshHint hint) {
    BoardVisual v = BoardVisual::fromPosition(pos_, node_ && !node_->isRoot() ? node_->move : Move::none());
    v.flipped = flipped_;
    if (selected_ != NoSquare) {
        v.selected = selected_;
        if (app_.settings().showLegalMoves)
            for (Move m : legal_)
                if (m.from() == selected_) v.targets |= bb(m.to());
    }
    for (const Rect& r : board_.update(v)) app_.invalidate(r, hint);
}

void AnalyzeScreen::refreshPanel() {
    app_.invalidate(statusRect_, RefreshHint::Content);
    app_.invalidate(movesRect_, RefreshHint::Content);
    updateButtons();
}

void AnalyzeScreen::updateButtons() {
    const bool back = node_ && !node_->isRoot();
    const bool fwd = node_ && !node_->children.empty();
    for (Button& b : buttons_) {
        bool en = b.enabled;
        if (b.id == BtnBack) en = back;
        if (b.id == BtnForward) en = fwd;
        if (en != b.enabled) {
            b.enabled = en;
            app_.invalidate(b.rect, RefreshHint::Content);
        }
    }
}

std::string AnalyzeScreen::statusText() const {
    const Outcome o = gameOutcome(pos_, tree_.keyHistoryTo(node_));
    std::string side = pos_.sideToMove() == White ? "White" : "Black";
    std::string s;
    if (o.over()) {
        s = std::string(terminationText(o.termination)) + " · " + (o.result == "1/2-1/2" ? "½-½" : o.result);
    } else {
        s = side + " to move";
        if (pos_.inCheck()) s += " · check";
        const auto history = tree_.keyHistoryTo(node_);
        if (canClaimThreefold(pos_, history)) s += " · threefold";
        else if (canClaimFiftyMoves(pos_)) s += " · 50 moves";
    }
    if (node_ && !node_->isMainLine()) s += " · variation";
    return s;
}

void AnalyzeScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    drawStatusBar(app_, c, "Analyze", batteryText(app_), "Home");
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());

    if (clip.intersects(statusRect_)) {
        Font& f = app_.fonts().get(s.fontPx, FontStyle::Bold);
        f.draw(c, statusRect_.x, statusRect_.y + f.ascent() + s.fontPx / 6, statusText(), t.ink);
    }
    if (clip.intersects(movesRect_)) drawMoves(c);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

void AnalyzeScreen::drawMoves(Canvas& c) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    Font& f = app_.fonts().get(s.fontPx);
    Font& fb = app_.fonts().get(s.fontPx, FontStyle::Bold);

    struct Token {
        std::string text;
        bool current = false;
        bool future = false;
    };
    std::vector<Token> tokens;
    // Path to the current node, then the main continuation after it (greyed).
    std::vector<const GameNode*> path;
    for (const GameNode* n = node_; n && !n->isRoot(); n = n->parent) path.push_back(n);
    std::reverse(path.begin(), path.end());
    Position p = tree_.startPosition();
    auto addMove = [&](const GameNode* n, bool future, bool forceNumber) {
        if (p.sideToMove() == White) tokens.push_back({std::to_string(p.fullmoveNumber()) + ".", false, future});
        else if (forceNumber) tokens.push_back({std::to_string(p.fullmoveNumber()) + "…", false, future});
        tokens.push_back({toSan(p, n->move), n == node_, future});
        p.makeMove(n->move);
    };
    bool first = true;
    for (const GameNode* n : path) {
        addMove(n, false, first);
        first = false;
    }
    for (const GameNode* n = node_ ? node_->mainChild() : nullptr; n; n = n->mainChild()) {
        addMove(n, true, first);
        first = false;
    }
    if (tokens.empty()) {
        f.draw(c, movesRect_.x, movesRect_.y + f.ascent(), "Tap a piece, then a square.", blend(t.background, t.ink, 150));
        return;
    }

    // Wrap tokens into lines and show the window that contains the current move.
    const int space = f.measure(" ");
    std::vector<std::vector<size_t>> lines(1);
    int x = 0, currentLine = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const int w = (tokens[i].current ? fb : f).measure(tokens[i].text) + space;
        if (x + w > movesRect_.w && !lines.back().empty()) {
            lines.emplace_back();
            x = 0;
        }
        lines.back().push_back(i);
        x += w;
        if (tokens[i].current) currentLine = static_cast<int>(lines.size()) - 1;
    }
    const int lh = f.lineHeight();
    const int visible = std::max(1, movesRect_.h / lh);
    int firstLine = std::max(0, currentLine - visible + 1);
    if (!node_ || node_->isRoot()) firstLine = 0;
    c.pushClip(movesRect_);
    int y = movesRect_.y;
    for (int li = firstLine; li < static_cast<int>(lines.size()) && li < firstLine + visible; ++li) {
        int px = movesRect_.x;
        for (size_t idx : lines[static_cast<size_t>(li)]) {
            const Token& tk = tokens[idx];
            Font& ff = tk.current ? fb : f;
            const int w = ff.measure(tk.text);
            if (tk.current) {
                c.fill({px - space / 2, y, w + space, lh}, t.ink);
                ff.draw(c, px, y + ff.ascent(), tk.text, t.background);
            } else {
                ff.draw(c, px, y + ff.ascent(), tk.text, tk.future ? blend(t.background, t.ink, 130) : t.ink);
            }
            px += w + space;
        }
        y += lh;
    }
    c.popClip();
}

void AnalyzeScreen::select(Square s) {
    if (s == selected_) return;
    selected_ = s;
    refreshBoard(RefreshHint::Selection);
}

void AnalyzeScreen::onSquare(Square s) {
    if (s == NoSquare) return;
    const Piece p = pos_.pieceAt(s);
    if (selected_ != NoSquare) {
        if (s == selected_) return select(NoSquare);
        bool isTarget = false;
        for (Move m : legal_)
            if (m.from() == selected_ && m.to() == s) isTarget = true;
        if (isTarget) return tryMove(selected_, s);
    }
    if (p != NoPiece && colorOf(p) == pos_.sideToMove()) {
        bool hasMoves = false;
        for (Move m : legal_) hasMoves |= m.from() == s;
        select(hasMoves ? s : NoSquare);
    } else {
        select(NoSquare);
    }
}

void AnalyzeScreen::tryMove(Square from, Square to) {
    std::vector<Move> matches;
    for (Move m : legal_)
        if (m.from() == from && m.to() == to) matches.push_back(m);
    if (matches.empty()) {
        select(NoSquare);
        return;
    }
    if (matches.size() == 1) return commitMove(matches.front());
    // Promotion: ask for the piece.
    const Color us = pos_.sideToMove();
    app_.push(std::make_unique<PromotionDialog>(app_, us, [this, matches](PieceType t) {
        if (t == NoPieceType) return select(NoSquare);
        for (Move m : matches)
            if (m.promotion() == t) return commitMove(m);
    }));
}

void AnalyzeScreen::commitMove(Move m) {
    util::Log::debug("analyze", "move " + toUci(m));
    setNode(tree_.addMove(node_, m));
}

bool AnalyzeScreen::playSan(const std::string& san) {
    auto m = parseSan(pos_, san);
    if (!m) return false;
    commitMove(m.value());
    return true;
}

void AnalyzeScreen::goBack() {
    if (node_ && node_->parent) setNode(node_->parent);
}

void AnalyzeScreen::goForward() {
    if (node_ && node_->mainChild()) setNode(node_->mainChild());
}

void AnalyzeScreen::flip() {
    flipped_ = !flipped_;
    refreshBoard(RefreshHint::Content);
}

bool AnalyzeScreen::loadFen(const std::string& fen) {
    auto p = parseFen(fen);
    if (!p) return false;
    reset(p.value());
    return true;
}

void AnalyzeScreen::editFen() {
    app_.requestText("Position (FEN)", toFen(pos_), [this](std::optional<std::string> text) {
        if (!text) return;
        auto p = parseFen(*text);
        if (!p) {
            app_.showMessage("Invalid FEN", p.error().context);
            return;
        }
        reset(p.value());
    });
}

void AnalyzeScreen::openMenu() {
    app_.push(std::make_unique<MessageDialog>(
        app_, "Analyze", "", std::vector<std::string>{"Flip board", "Edit FEN", "New game", "Home", "Close"},
        [this](int i) {
            switch (i) {
            case 0: flip(); break;
            case 1: editFen(); break;
            case 2: reset(Position::startPosition()); break;
            case 3: app_.pop(); break;
            default: break;
            }
        },
        true));
}

void AnalyzeScreen::onGesture(const Gesture& g) {
    if (g.type == Gesture::Type::Drag) {
        const Square from = board_.squareAt(g.start);
        const Square to = board_.squareAt(g.end);
        if (app_.settings().dragToMove && from != NoSquare && to != NoSquare) {
            const Piece p = pos_.pieceAt(from);
            if (p != NoPiece && colorOf(p) == pos_.sideToMove()) tryMove(from, to);
        }
        return;
    }
    if (g.type == Gesture::Type::LongPress) {
        if (app_.layout().board.contains(g.start)) openMenu();
        return;
    }
    if (statusBackRect(app_).contains(g.start)) {
        app_.pop();
        return;
    }
    if (app_.layout().board.contains(g.start)) {
        onSquare(board_.squareAt(g.start));
        return;
    }
    switch (hitTest(buttons_, g.start)) {
    case BtnBack: goBack(); break;
    case BtnForward: goForward(); break;
    case BtnFlip: flip(); break;
    case BtnFen: editFen(); break;
    case BtnNew: reset(Position::startPosition()); break;
    default: break;
    }
}

void AnalyzeScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: goBack(); break;
    case KeyCode::Next: case KeyCode::Right: goForward(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

} // namespace pchess::app
