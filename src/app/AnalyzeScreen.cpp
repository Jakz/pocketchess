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
#include "core/util/Files.h"
#include "core/util/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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

AnalyzeScreen::AnalyzeScreen(Application& app, GameTree game, const std::vector<Move>& path) : Screen(app) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    tree_ = std::move(game);
    GameNode* n = path.empty() ? tree_.mainLineEnd() : tree_.root();
    for (Move m : path) {
        GameNode* next = nullptr;
        for (const auto& c : n->children)
            if (c->move == m) next = c.get();
        if (!next) break;
        n = next;
    }
    setNode(n);
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
    // Engine: the headline (evaluation, best move, depth) sits at the right of the status row; the
    // PV and one small line per extra MultiPV line (Settings -> Analysis lines) go below it. Areas
    // never overlap: the move list keeps at least one line and extra lines that do not fit are
    // dropped (a repaint of one area must never touch another, e.g. the button row).
    const int avail = std::max(0, buttonsRect_.y - pad - statusRect_.bottom());
    const int minMoves = std::min(avail, app_.fonts().get(s.fontPx).lineHeight());
    const int small = app_.fonts().get(s.smallPx).lineHeight();
    moreLinesFit_ = std::clamp(app_.settings().analysisMultiPv, 1, 3) - 1;
    int engineH = 0;
    if (engineMode_ != EngineMode::Off) {
        while (moreLinesFit_ > 0 && (1 + moreLinesFit_) * small + pad / 2 > avail - minMoves) --moreLinesFit_;
        engineH = std::min((1 + moreLinesFit_) * small + pad / 2, avail - minMoves);
    }
    engineRect_ = {statusRect_.x, statusRect_.bottom(), statusRect_.w, engineH};
    movesRect_ = {p.x + pad * 2, engineRect_.bottom(), p.w - pad * 4, avail - engineH};

    const char* labels[] = {"◀", "▶", "Flip", "Engine", "Menu"};
    const auto cells = splitRow(buttonsRect_, 5, pad);
    const bool first = buttons_.empty();
    if (first) {
        for (int i = 0; i < 5; ++i) {
            Button b;
            b.id = i;
            b.label = labels[i];
            if (i < 2) b.hint = "move";  // the Prev/Next keys
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
    for (const Rect& r : board_.update(v)) app_.invalidateExact(r, hint);
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
    drawStatusBar(app_, c, *this, "Analyze", batteryText(app_));
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());

    if (clip.intersects(statusRect_)) {
        Font& f = app_.fonts().get(s.fontPx, FontStyle::Bold);
        const int baseline = statusRect_.y + f.ascent() + s.fontPx / 6;
        c.pushClip(statusRect_);
        const std::string head = engineMode_ == EngineMode::Off ? std::string() : shown_.head;
        const int headW = head.empty() ? 0 : f.measure(head);
        f.draw(c, statusRect_.x, baseline, fitText(f, statusText(), statusRect_.w - headW - s.fontPx), t.ink);
        if (!head.empty()) f.draw(c, statusRect_.right() - headW, baseline, head, t.ink);
        c.popClip();
    }
    if (!engineRect_.empty() && clip.intersects(engineRect_)) drawEngine(c);
    if (clip.intersects(movesRect_)) drawMoves(c);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

void AnalyzeScreen::drawMoves(Canvas& c) {
    Rect list = movesRect_;
    if (node_ && !node_->comment.empty()) {
        // The current move's comment first (one line, cut at the panel width), then the moves.
        const BoardTheme& t = app_.theme();
        Font& small = app_.fonts().get(app_.style().smallPx);
        std::string text = "“" + node_->comment + "”";
        const auto lines = small.wrap(text, list.w);
        if (!lines.empty()) text = lines.size() > 1 ? lines.front() + "…" : lines.front();
        c.pushClip(list);
        small.draw(c, list.x, list.y + small.ascent(), text, blend(t.background, t.ink, 170));
        c.popClip();
        list.y += small.lineHeight();
        list.h -= small.lineHeight();
    }
    drawMoveList(app_, c, list, tree_, node_, "Tap a piece, then a square.");
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
        app_, "Analyze", "", std::vector<std::string>{"Move…", "Position…", next, backMenuLabel(app_, *this), "Close"},
        [this](int i) {
            switch (i) {
            case 0: openMoveMenu(); break;
            case 1: openPositionMenu(); break;
            case 2: cycleEngineMode(); break;
            case 3: app_.pop(); break;
            default: break;
            }
        },
        true));
}

void AnalyzeScreen::openMoveMenu() {
    // Only the actions that apply to the current move.
    enum Action { Comment, MainLine, Promote, Delete };
    std::vector<std::string> labels;
    std::vector<Action> actions;
    auto add = [&](const char* label, Action a) {
        labels.push_back(label);
        actions.push_back(a);
    };
    const bool onMove = node_ && !node_->isRoot();
    add(node_ && !node_->comment.empty() ? "Edit comment…" : "Comment…", Comment);
    if (onMove && !node_->isMainLine()) {
        add("Back to main line", MainLine);
        add("Promote variation", Promote);
    }
    if (onMove) add("Delete move", Delete);
    labels.push_back("Close");
    app_.push(std::make_unique<MessageDialog>(
        app_, "Move", "", labels,
        [this, actions](int i) {
            if (i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Comment:
                app_.requestText("Comment", node_->comment, [this](std::optional<std::string> text) {
                    if (text) setComment(*text);
                });
                break;
            case MainLine: goToMainLine(); break;
            case Promote: promoteVariation(); break;
            case Delete: deleteMove(); break;
            }
        },
        true));
}

void AnalyzeScreen::openPositionMenu() {
    app_.push(std::make_unique<MessageDialog>(
        app_, "Position", "", std::vector<std::string>{"Flip board", "Edit FEN", "Load PGN…", "New game", "Close"},
        [this](int i) {
            switch (i) {
            case 0: flip(); break;
            case 1: editFen(); break;
            case 2: loadPgnFromImports(); break;
            case 3: reset(Position::startPosition()); break;
            default: break;
            }
        },
        true));
}

void AnalyzeScreen::loadPgnFromImports() {
    const std::filesystem::path dir = app_.paths().imports();
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
        std::string ext = de.path().extension().string();
        for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (de.is_regular_file(ec) && ext == ".pgn") files.push_back(de.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        app_.showMessage("No PGN files", "Copy .pgn files into " + dir.string() + " and try again.");
        return;
    }
    std::vector<std::string> names;
    for (const auto& f : files) names.push_back(f.filename().string());
    std::weak_ptr<char> alive = alive_;
    chooseFromList(app_, "Load PGN", names, [this, alive, files](int fi) {
        if (!alive.expired() && fi >= 0) loadPgnFile(files[static_cast<size_t>(fi)]);
    });
}

void AnalyzeScreen::loadPgnFile(const std::filesystem::path& file) {
    std::weak_ptr<char> alive = alive_;
    auto text = util::readTextFile(file, 16u << 20);
    if (!text) {
        app_.showMessage("Cannot read the file", text.error().message());
        return;
    }
    auto parsed = std::make_shared<std::vector<PgnGame>>();
    std::string firstError;
    int errors = 0;
    for (auto& g : parsePgn(*text)) {
        if (g) {
            parsed->push_back(std::move(g.value()));
        } else {
            ++errors;
            if (firstError.empty()) firstError = g.error().message();
        }
    }
    util::Log::info("analyze", file.filename().string() + ": " + std::to_string(parsed->size()) + " game(s), " +
                                   std::to_string(errors) + " skipped");
    if (parsed->empty()) {
        app_.showMessage("No games", firstError.empty() ? "The file has no games." : firstError);
        return;
    }
    auto load = [this, parsed](int gi) {
        PgnGame& g = (*parsed)[static_cast<size_t>(gi)];
        tree_ = std::move(g.tree);
        setNode(tree_.root());  // from the start: step through with Next
        app_.invalidate(app_.layout().panel, RefreshHint::Content);
    };
    if (parsed->size() == 1) return load(0);
    std::vector<std::string> labels;
    // "White – Black · result", each name shortened on its own so both stay visible.
    auto shortName = [](std::string s) {
        if (s.empty() || s == "?") return std::string("?");
        if (s.size() <= 18) return s;
        size_t cut = 17;  // on a UTF-8 character boundary
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        return s.substr(0, cut) + "…";
    };
    for (const PgnGame& g : *parsed)
        labels.push_back(shortName(g.header("White")) + " – " + shortName(g.header("Black")) + " · " +
                         (g.result == "1/2-1/2" ? "½-½" : g.result));
    chooseFromList(app_, file.filename().string(), labels, [alive, load](int gi) {
        if (!alive.expired() && gi >= 0) load(gi);
    });
}

void AnalyzeScreen::setComment(const std::string& text) {
    if (!node_) return;
    node_->comment = text;
    app_.invalidate(movesRect_, RefreshHint::Content);
    app_.sessionChanged();
}

void AnalyzeScreen::goToMainLine() {
    GameNode* n = node_;
    while (n && !n->isMainLine()) n = n->parent;  // the fork point on the main line
    if (n && n != node_) setNode(n);
}

void AnalyzeScreen::promoteVariation() {
    if (!node_ || node_->isRoot() || node_->isMainLine()) return;
    tree_.promoteVariation(node_);
    setNode(node_);  // redraws the list and saves the session
}

void AnalyzeScreen::deleteMove() {
    if (!node_ || node_->isRoot()) return;
    GameNode* parent = node_->parent;
    tree_.deleteNode(node_);
    setNode(parent);
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
        updateEngineText({pos_.inCheck() ? "Checkmate" : "Stalemate", "", {}}, true);
        return;
    }
    engine::EngineManager& em = app_.engines();
    std::weak_ptr<char> alive = alive_;
    const bool warm = em.processRunning();
    lines_.clear();
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
    updateEngineText({warm ? "Engine: thinking…" : "Engine: starting…", "", {}}, true);
}

void AnalyzeScreen::stopAnalysis() {
    if (!job_) return;
    const engine::JobId id = job_;
    job_ = 0;  // results of the cancelled job are ignored
    app_.engines().cancel(id);
}

void AnalyzeScreen::onEngineInfo(engine::JobId id, const engine::SearchInfo& info) {
    const int k = info.multipv;
    if (id != job_ || k < 1 || k > 3) return;
    if (lines_.size() < static_cast<size_t>(k)) lines_.resize(static_cast<size_t>(k));
    lines_[static_cast<size_t>(k - 1)] = info;
    if (lines_.front().depth == 0) return;  // line 1 first
    updateEngineText(composeLines(false), false);
}

AnalyzeScreen::EngineText AnalyzeScreen::composeLines(bool final) const {
    EngineText t = formatInfo(lines_.front(), final);
    for (size_t i = 1; i < lines_.size(); ++i)
        if (lines_[i].depth > 0) t.more.push_back(formatExtraLine(lines_[i]));
    return t;
}

// "2) +0.10  a4 a5 Nf3": evaluation and the first moves of a secondary line.
std::string AnalyzeScreen::formatExtraLine(const engine::SearchInfo& info) const {
    std::string s = std::to_string(info.multipv) + ") ";
    if (info.score) s += engine::formatScore(*info.score, pos_.sideToMove() == White, app_.settings().evalFromWhite);
    s += " ";
    Position p = pos_;
    for (size_t i = 0; i < info.pv.size() && i < 6; ++i) {
        s += " " + toSan(p, info.pv[i]);
        p.makeMove(info.pv[i]);
    }
    return s;
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
        updateEngineText({"Engine error", r.error, {}}, true);
        return;
    }
    if (!r.lines.empty()) {
        lines_ = r.lines;
        lines_.resize(std::min<size_t>(lines_.size(), 3));
        updateEngineText(composeLines(true), false);
    }
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
        app_.invalidate(statusRect_, RefreshHint::Content);  // the headline is on the status row
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
    Font& small = app_.fonts().get(s.smallPx);
    c.pushClip(engineRect_);
    if (!shown_.pv.empty()) {
        std::string pv = shown_.pv;
        while (!pv.empty() && small.measure(pv) > engineRect_.w) {
            const size_t sp = pv.rfind(' ');
            pv = sp == std::string::npos ? std::string() : pv.substr(0, sp);
        }
        small.draw(c, engineRect_.x, engineRect_.y + small.ascent(), pv, blend(t.background, t.ink, 170));
    }
    int y = engineRect_.y + small.lineHeight();
    for (size_t i = 0; i < shown_.more.size() && static_cast<int>(i) < moreLinesFit_; ++i) {
        small.draw(c, engineRect_.x, y + small.ascent(), shown_.more[i], t.ink);
        y += small.lineHeight();
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
    // Back from Settings: "Analysis lines" may have changed the panel's height.
    const Rect oldEngine = engineRect_;
    layoutPanel();
    if (engineRect_ != oldEngine) {
        shown_.more.clear();
        app_.invalidate(app_.layout().panel, RefreshHint::Content);
    }
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
