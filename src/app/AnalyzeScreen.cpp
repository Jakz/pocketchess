// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AnalyzeScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/DiagnosticsScreen.h"
#include "app/Dialogs.h"
#include "app/MoveList.h"
#include "app/SettingsScreen.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/Pgn.h"
#include "core/chess/Rules.h"
#include "core/chess/San.h"
#include "core/util/Log.h"

#include <algorithm>

namespace pchess::app {

using namespace ui;
using namespace chess;

namespace {
const char* buttonsHint(AnalyzeScreen::EngineMode m) {
    switch (m) {
    case AnalyzeScreen::EngineMode::Off: return "Off";
    case AnalyzeScreen::EngineMode::Quick: return "Quick";
    case AnalyzeScreen::EngineMode::Deep: return "Deep";
    }
    return "?";
}
} // namespace

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

AnalyzeScreen::AnalyzeScreen(Application& app, GameTree game) : Screen(app) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    tree_ = std::move(game);
    setNode(tree_.mainLineEnd());
    app_.invalidate(app_.layout().panel, RefreshHint::Content);
}

AnalyzeScreen::~AnalyzeScreen() {
    alive_.reset();
    if (throttleTimer_) app_.host().cancelTimer(throttleTimer_);
    if (job_) app_.engines().cancel(job_);
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
    // Engine lines (evaluation + PV) between the status line and the move list, when on.
    const int engineH = engineMode_ == EngineMode::Off
                            ? 0
                            : app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() +
                                  app_.fonts().get(s.smallPx).lineHeight() + pad / 2;
    engineRect_ = {statusRect_.x, statusRect_.bottom(), statusRect_.w, engineH};
    movesRect_ = {p.x + pad * 2, engineRect_.bottom(), p.w - pad * 4, buttonsRect_.y - pad - engineRect_.bottom()};

    const char* labels[] = {"◀ Back", "Next ▶", "Flip", "Engine", "Menu"};
    const auto cells = splitRow(buttonsRect_, 5, pad);
    const bool first = buttons_.empty();
    if (first) {
        for (int i = 0; i < 5; ++i) {
            Button b;
            b.id = i;
            b.label = labels[i];
            buttons_.push_back(b);
        }
    }
    for (int i = 0; i < 5; ++i) buttons_[static_cast<size_t>(i)].rect = cells[static_cast<size_t>(i)];
    Button& eng = buttons_[BtnEngine];
    eng.hint = buttonsHint(engineMode_);
    eng.primary = engineMode_ != EngineMode::Off;
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
    if (engineMode_ != EngineMode::Off) startAnalysis();
    app_.sessionChanged();
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
    app_.invalidate(engineRect_, RefreshHint::Content);
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
    if (!engineRect_.empty() && clip.intersects(engineRect_)) drawEngine(c);
    if (clip.intersects(movesRect_)) drawMoves(c);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

void AnalyzeScreen::drawMoves(Canvas& c) {
    drawMoveList(app_, c, movesRect_, tree_, node_, "Tap a piece, then a square.");
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
    app_.sessionChanged();
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
    const char* next = engineMode_ == EngineMode::Off     ? "Engine: Quick"
                       : engineMode_ == EngineMode::Quick ? "Engine: Deep"
                                                          : "Engine: Off";
    app_.push(std::make_unique<MessageDialog>(
        app_, "Analyze", "", std::vector<std::string>{"Flip board", "Edit FEN", "New game", next, "Home", "Close"},
        [this](int i) {
            switch (i) {
            case 0: flip(); break;
            case 1: editFen(); break;
            case 2: reset(Position::startPosition()); break;
            case 3: cycleEngineMode(); break;
            case 4: app_.pop(); break;
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
        if (app_.settings().tapToMove) onSquare(board_.squareAt(g.start));
        return;
    }
    switch (hitTest(buttons_, g.start)) {
    case BtnBack: goBack(); break;
    case BtnForward: goForward(); break;
    case BtnFlip: flip(); break;
    case BtnEngine: cycleEngineMode(); break;
    case BtnMenu: openMenu(); break;
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

// --- Engine panel ------------------------------------------------------------------------------

void AnalyzeScreen::cycleEngineMode() {
    switch (engineMode_) {
    case EngineMode::Off: setEngineMode(EngineMode::Quick); break;
    case EngineMode::Quick: setEngineMode(EngineMode::Deep); break;
    case EngineMode::Deep: setEngineMode(EngineMode::Off); break;
    }
}

void AnalyzeScreen::setEngineMode(EngineMode m) {
    if (m != EngineMode::Off && !app_.engines().available(engine::Role::Analysis)) {
        const auto& entries = app_.engineRegistry().entries();
        std::string detail = "No analysis engine is installed in engines/.";
        if (!entries.empty() && !entries.front().problem.empty())
            detail = entries.front().descriptor.id + ": " + entries.front().problem;
        showEngineMissing(detail);
        m = EngineMode::Off;
    }
    const bool relayout = (m == EngineMode::Off) != (engineMode_ == EngineMode::Off);
    engineMode_ = m;
    util::Log::info("analyze", std::string("engine ") + buttonsHint(m));
    stopAnalysis();
    shown_ = pending_ = {};
    layoutPanel();
    if (relayout) app_.invalidate(app_.layout().panel, RefreshHint::Content);
    else app_.invalidate(buttons_[BtnEngine].rect, RefreshHint::Content);
    if (m != EngineMode::Off) startAnalysis();
}

void AnalyzeScreen::startAnalysis() {
    stopAnalysis();
    if (engineMode_ == EngineMode::Off || app_.top() != this) return;
    if (legal_.empty()) {
        updateEngineText({pos_.inCheck() ? "Checkmate" : "Stalemate", ""}, true);
        return;
    }
    engine::EngineManager& em = app_.engines();
    std::weak_ptr<char> alive = alive_;
    const bool warm = em.processRunning();
    job_ = em.analyze(
        tree_.startPosition(), tree_.movesTo(node_),
        engineMode_ == EngineMode::Quick ? engine::AnalysisMode::Quick : engine::AnalysisMode::Deep,
        app_.settings().analysisMultiPv,
        [this, alive](engine::JobId id, const engine::SearchInfo& info) {
            if (!alive.expired()) onEngineInfo(id, info);
        },
        [this, alive](const engine::JobResult& r) {
            if (!alive.expired()) onEngineDone(r);
        });
    updateEngineText({warm ? "Engine: thinking…" : "Engine: starting…", ""}, true);
}

void AnalyzeScreen::stopAnalysis() {
    if (!job_) return;
    const engine::JobId id = job_;
    job_ = 0;  // results of the cancelled job are ignored
    app_.engines().cancel(id);
}

void AnalyzeScreen::onEngineInfo(engine::JobId id, const engine::SearchInfo& info) {
    if (id != job_ || info.multipv != 1) return;
    updateEngineText(formatInfo(info, false), false);
}

void AnalyzeScreen::onEngineDone(const engine::JobResult& r) {
    if (r.id != job_) return;
    job_ = 0;
    if (!r.ok && !r.cancelled) {
        if (r.errorCode == ErrorCode::EngineMissing) {
            setEngineMode(EngineMode::Off);
            showEngineMissing(r.error);
            return;
        }
        updateEngineText({"Engine error", r.error}, true);
        return;
    }
    if (!r.lines.empty()) updateEngineText(formatInfo(r.lines.front(), true), false);
}

AnalyzeScreen::EngineText AnalyzeScreen::formatInfo(const engine::SearchInfo& info, bool final) const {
    EngineText t;
    const bool white = pos_.sideToMove() == White;
    std::string eval = "?";
    if (info.score) {
        engine::Score sc = *info.score;
        eval = engine::formatScore(sc, white, app_.settings().evalFromWhite);
        if (sc.lowerbound || sc.upperbound) eval += sc.lowerbound ? "↑" : "↓";
    }
    std::string best;
    if (!info.pv.empty()) best = toSan(pos_, info.pv.front());
    t.head = eval;
    if (!best.empty()) t.head += "   " + best;
    t.head += "   depth " + std::to_string(info.depth);
    if (final && engineMode_ == EngineMode::Deep) t.head += "  · done";
    // PV with move numbers: "12. Nf3 Nf6 13. c4" / "12… Nf6 13. c4".
    Position p = pos_;
    bool firstMove = true;
    for (const Move m : info.pv) {
        if (p.sideToMove() == White) t.pv += std::to_string(p.fullmoveNumber()) + ". ";
        else if (firstMove) t.pv += std::to_string(p.fullmoveNumber()) + "… ";
        t.pv += toSan(p, m) + " ";
        p.makeMove(m);
        firstMove = false;
        if (t.pv.size() > 160) break;
    }
    while (!t.pv.empty() && t.pv.back() == ' ') t.pv.pop_back();
    return t;
}

// Throttled redraw (architecture doc §8.5): at most one partial refresh per second, and only
// when the text changed. `now` forces an immediate redraw (user actions).
void AnalyzeScreen::updateEngineText(EngineText t, bool now) {
    pending_ = std::move(t);
    if (pending_ == shown_) return;
    const auto current = app_.host().now();
    const auto elapsed = current - lastEngineDraw_;
    if (now || elapsed >= std::chrono::seconds(1)) {
        if (throttleTimer_) {
            app_.host().cancelTimer(throttleTimer_);
            throttleTimer_ = 0;
        }
        shown_ = pending_;
        lastEngineDraw_ = current;
        app_.invalidate(engineRect_, RefreshHint::Content);
        return;
    }
    if (throttleTimer_) return;
    std::weak_ptr<char> alive = alive_;
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(1) - elapsed);
    throttleTimer_ = app_.host().startTimer(wait, [this, alive] {
        if (alive.expired()) return;
        throttleTimer_ = 0;
        updateEngineText(pending_, true);
    });
}

void AnalyzeScreen::drawEngine(Canvas& c) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    Font& fb = app_.fonts().get(s.fontPx, FontStyle::Bold);
    Font& small = app_.fonts().get(s.smallPx);
    c.pushClip(engineRect_);
    fb.draw(c, engineRect_.x, engineRect_.y + fb.ascent(), shown_.head, t.ink);
    if (!shown_.pv.empty()) {
        std::string pv = shown_.pv;
        while (!pv.empty() && small.measure(pv) > engineRect_.w) {
            const size_t sp = pv.rfind(' ');
            pv = sp == std::string::npos ? std::string() : pv.substr(0, sp);
        }
        small.draw(c, engineRect_.x, engineRect_.y + fb.lineHeight() + small.ascent(), pv,
                   blend(t.background, t.ink, 170));
    }
    c.popClip();
}

void AnalyzeScreen::showEngineMissing(const std::string& detail) {
    util::Log::warn("analyze", "engine missing: " + detail);
    app_.push(std::make_unique<MessageDialog>(
        app_, "Engine missing",
        detail + "\n\nAnalysis needs an engine in engines/<id>/ (see Diagnostics). Everything else works without one.",
        std::vector<std::string>{"Settings", "Diagnostics", "Continue"}, [this](int i) {
            if (i == 0) app_.push(std::make_unique<SettingsScreen>(app_));
            if (i == 1) app_.push(std::make_unique<DiagnosticsScreen>(app_));
        }));
}

void AnalyzeScreen::onEnter() {
    if (engineMode_ != EngineMode::Off && !job_) startAnalysis();
}

void AnalyzeScreen::onLeave() {
    // Covered by a dialog or another screen: the engine stops (battery); onEnter resumes.
    stopAnalysis();
}

void AnalyzeScreen::onLifecycle(platform::Lifecycle l) {
    if (l == platform::Lifecycle::Background || l == platform::Lifecycle::Sleep || l == platform::Lifecycle::Exit) {
        if (engineMode_ != EngineMode::Off) setEngineMode(EngineMode::Off);
    }
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool AnalyzeScreen::saveSession(std::map<std::string, std::string>& out) const {
    // The whole tree as PGN (variations and comments survive), plus the path to the current node.
    std::string pgn;
    const std::string fen = toFen(tree_.startPosition());
    if (fen != toFen(Position::startPosition())) pgn += "[SetUp \"1\"]\n[FEN \"" + fen + "\"]\n\n";
    pgn += writeMovetext(tree_) + " *\n";
    std::vector<const GameNode*> path;
    for (const GameNode* n = node_; n && !n->isRoot(); n = n->parent) path.push_back(n);
    std::string moves;
    for (auto it = path.rbegin(); it != path.rend(); ++it) moves += (moves.empty() ? "" : " ") + toUci((*it)->move);
    out["analyze.pgn"] = pgn;
    out["analyze.path"] = moves;
    out["analyze.flipped"] = flipped_ ? "true" : "false";
    const size_t plies = tree_.mainLine().size();
    out["summary"] = "Analysis board: " + std::to_string(plies) + (plies == 1 ? " move" : " moves") + ".";
    return true;
}

std::unique_ptr<AnalyzeScreen> AnalyzeScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto get = [&](const char* k) {
        auto it = s.find(k);
        return it == s.end() ? std::string() : it->second;
    };
    auto game = parsePgnGame(get("analyze.pgn"));
    if (!game) {
        util::Log::warn("analyze", "session: " + game.error().message());
        return nullptr;
    }
    auto a = std::make_unique<AnalyzeScreen>(app, std::move(game->tree));
    // Follow the saved path; stop where it no longer matches (the tree is the source of truth).
    GameNode* n = a->tree_.root();
    const std::string path = get("analyze.path");
    size_t i = 0;
    while (i < path.size()) {
        size_t j = path.find(' ', i);
        if (j == std::string::npos) j = path.size();
        const std::string uci = path.substr(i, j - i);
        GameNode* next = nullptr;
        for (const auto& child : n->children)
            if (toUci(child->move) == uci) next = child.get();
        if (!next) break;
        n = next;
        i = j + 1;
    }
    a->flipped_ = get("analyze.flipped") == "true";
    a->setNode(n);
    return a;
}

} // namespace pchess::app
