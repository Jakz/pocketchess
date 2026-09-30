// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/PlayScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "app/MoveList.h"
#include "app/SettingsScreen.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/Pgn.h"
#include "core/chess/Rules.h"
#include "core/chess/San.h"
#include "core/storage/ChessDb.h"
#include "core/util/Clock.h"
#include "core/util/Log.h"
#include "ui/canvas/PngWriter.h"

#include <charconv>
#include <cstdio>

namespace pchess::app {

using namespace ui;
using namespace chess;

namespace {

std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

const char* winner(const std::string& result) {
    return result == "1-0" ? "White" : result == "0-1" ? "Black" : "";
}

} // namespace

PlayScreen::PlayScreen(Application& app, PlaySetup setup) : Screen(app), setup_(std::move(setup)) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    Position start = Position::startPosition();
    if (!setup_.fen.empty()) {
        if (auto p = parseFen(setup_.fen)) start = p.value();
        else util::Log::warn("play", p.error().message());
    }
    tree_ = GameTree(start);
    flipped_ = setup_.human == Black;
    startedAt_ = util::SystemClock::instance().unixSeconds();
    if (const engine::EngineDescriptor* d = app_.engines().engineFor(engine::Role::Play)) engineName_ = d->name;
    util::Log::info("play", std::string("new game: human ") + (setup_.human == White ? "white" : "black") +
                                ", strength " + setup_.strength + ", engine " +
                                (engineName_.empty() ? "none" : engineName_));
    setNode(tree_.root());
}

PlayScreen::~PlayScreen() {
    alive_.reset();
    if (job_) app_.engines().cancel(job_);
}

void PlayScreen::layoutPanel() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect p = l.panel;
    const int btnH = std::min(kMinTouchPx, p.h / 3);
    const int statusH = app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() + pad;
    statusRect_ = {p.x + pad * 2, p.y + pad, p.w - pad * 4, statusH};
    buttonsRect_ = {p.x + pad, p.bottom() - pad - btnH, p.w - 2 * pad, btnH};
    movesRect_ = {p.x + pad * 2, statusRect_.bottom(), p.w - pad * 4, buttonsRect_.y - pad - statusRect_.bottom()};
    const char* labels[] = {"◀", "Flip", "Draw", "Resign", "Menu"};  // ◀ = the Prev key: undo
    const auto cells = splitRow(buttonsRect_, 5, pad);
    for (int i = 0; i < 5; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        if (i == 0) b.hint = "undo";
        b.rect = cells[static_cast<size_t>(i)];
        buttons_.push_back(b);
    }
}

// --- State ---------------------------------------------------------------------------------------

bool PlayScreen::humanToMove() const { return !finished_ && pos_.sideToMove() == setup_.human; }

bool PlayScreen::canClaimDraw() const {
    if (!humanToMove()) return false;
    return canClaimThreefold(pos_, tree_.keyHistoryTo(node_)) || canClaimFiftyMoves(pos_);
}

void PlayScreen::setNode(GameNode* node) {
    node_ = node;
    pos_ = tree_.positionAt(node_);
    generateLegalMoves(pos_, legal_);
    selected_ = NoSquare;
    refreshBoard(RefreshHint::Content);
    refreshPanel();
}

void PlayScreen::refreshBoard(RefreshHint hint) {
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

void PlayScreen::refreshPanel() {
    app_.invalidate(statusRect_, RefreshHint::Content);
    app_.invalidate(movesRect_, RefreshHint::Content);
    updateButtons();
}

void PlayScreen::updateButtons() {
    // Undo needs a move of the human's to take back.
    bool undoable = false;
    for (const GameNode* n = node_; n && !n->isRoot(); n = n->parent)
        if (tree_.positionAt(n->parent).sideToMove() == setup_.human) undoable = true;
    for (Button& b : buttons_) {
        bool en = b.enabled;
        if (b.id == BtnUndo) en = !finished_ && undoable;
        if (b.id == BtnDraw) en = canClaimDraw();
        if (b.id == BtnResign) en = !finished_;
        if (en != b.enabled) {
            b.enabled = en;
            app_.invalidate(b.rect, RefreshHint::Content);
        }
    }
}

std::string PlayScreen::statusText() const {
    if (finished_) return reason_ + " · " + (result_ == "1/2-1/2" ? "½-½" : result_);
    std::string s;
    if (humanToMove()) {
        s = "Your move";
        if (pos_.inCheck()) s += " · check";
        if (canClaimDraw()) s += " · draw claimable";
    } else {
        s = job_ ? "Engine is thinking…" : "Engine to move";
    }
    return s;
}

// --- Drawing -------------------------------------------------------------------------------------

void PlayScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    const std::string title = "Play · " + capitalized(setup_.strength);
    drawStatusBar(app_, c, *this, title, batteryText(app_));
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());
    if (clip.intersects(statusRect_)) {
        Font& f = app_.fonts().get(s.fontPx, FontStyle::Bold);
        f.draw(c, statusRect_.x, statusRect_.y + f.ascent() + s.fontPx / 6, statusText(), t.ink);
    }
    if (clip.intersects(movesRect_))
        drawMoveList(app_, c, movesRect_, tree_, node_,
                     setup_.human == White ? "You play White: tap a piece, then a square." : "You play Black.");
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

// --- Human moves ---------------------------------------------------------------------------------

void PlayScreen::select(Square s) {
    if (s == selected_) return;
    selected_ = s;
    refreshBoard(RefreshHint::Selection);
}

void PlayScreen::onSquare(Square s) {
    if (s == NoSquare || !humanToMove()) return;
    const Piece p = pos_.pieceAt(s);
    if (selected_ != NoSquare) {
        if (s == selected_) return select(NoSquare);
        for (Move m : legal_)
            if (m.from() == selected_ && m.to() == s) return tryMove(selected_, s);
    }
    if (p != NoPiece && colorOf(p) == pos_.sideToMove()) {
        bool hasMoves = false;
        for (Move m : legal_) hasMoves |= m.from() == s;
        select(hasMoves ? s : NoSquare);
    } else {
        select(NoSquare);
    }
}

void PlayScreen::tryMove(Square from, Square to) {
    std::vector<Move> matches;
    for (Move m : legal_)
        if (m.from() == from && m.to() == to) matches.push_back(m);
    if (matches.empty()) return select(NoSquare);
    if (matches.size() == 1) return commitMove(matches.front());
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<PromotionDialog>(app_, pos_.sideToMove(), [this, alive, matches](PieceType t) {
        if (alive.expired()) return;
        if (t == NoPieceType) return select(NoSquare);
        for (Move m : matches)
            if (m.promotion() == t) return commitMove(m);
    }));
}

bool PlayScreen::playSan(const std::string& san) {
    if (!humanToMove()) return false;
    auto m = parseSan(pos_, san);
    if (!m) return false;
    commitMove(m.value());
    return true;
}

void PlayScreen::commitMove(Move m) {
    util::Log::debug("play", "you " + toUci(m));
    setNode(tree_.addMove(node_, m));
    afterMove();
}

void PlayScreen::afterMove() {
    const Outcome o = gameOutcome(pos_, tree_.keyHistoryTo(node_));
    if (o.over()) return finish(o.result, terminationText(o.termination));
    app_.sessionChanged();
    if (!humanToMove()) requestEngineMove();
}

// --- Engine --------------------------------------------------------------------------------------

void PlayScreen::requestEngineMove() {
    if (finished_ || job_ || humanToMove()) return;
    if (!app_.engines().available(engine::Role::Play)) {
        showEngineProblem("Engine missing",
                          "Playing needs an engine in engines/<id>/ (see Diagnostics). Analyze works without one.");
        return;
    }
    std::weak_ptr<char> alive = alive_;
    job_ = app_.engines().bestMove(tree_.startPosition(), tree_.movesTo(node_), setup_.strength,
                                   [this, alive](const engine::JobResult& r) {
                                       if (!alive.expired()) onEngineDone(r);
                                   });
    app_.invalidate(statusRect_, RefreshHint::Content);
}

void PlayScreen::cancelEngine() {
    if (!job_) return;
    const engine::JobId id = job_;
    job_ = 0;  // its result is ignored from now on
    app_.engines().cancel(id);
}

void PlayScreen::onEngineDone(const engine::JobResult& r) {
    if (r.id != job_) return;  // cancelled (undo, leave) or superseded
    job_ = 0;
    if (r.cancelled) {
        app_.invalidate(statusRect_, RefreshHint::Content);
        return;
    }
    if (!r.ok || r.best.isNone()) {
        showEngineProblem("Engine failed", r.error.empty() ? "The engine returned no move." : r.error);
        return;
    }
    util::Log::debug("play", "engine " + toUci(r.best));
    setNode(tree_.addMove(node_, r.best));
    afterMove();
}

void PlayScreen::showEngineProblem(const std::string& title, const std::string& text) {
    engineBlocked_ = true;
    util::Log::warn("play", title + ": " + text);
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, title, text, std::vector<std::string>{"Retry", "Settings", backMenuLabel(app_, *this)}, [this, alive](int i) {
            if (alive.expired()) return;
            if (i == 0) {
                engineBlocked_ = false;
                requestEngineMove();
            }
            if (i == 1) app_.push(std::make_unique<SettingsScreen>(app_));
            if (i == 2) app_.pop();
        }));
}

// --- Game control --------------------------------------------------------------------------------

void PlayScreen::undo() {
    if (finished_) return;
    GameNode* n = node_;
    cancelEngine();
    // Back to the last position where the human was to move, one full move earlier.
    auto humanMoved = [&](const GameNode* x) { return tree_.positionAt(x->parent).sideToMove() == setup_.human; };
    while (n && !n->isRoot() && !humanMoved(n)) n = n->parent;  // skip the engine's reply
    if (!n || n->isRoot()) return;                              // nothing of ours to take back
    GameNode* parent = n->parent;
    tree_.deleteNode(n);
    util::Log::info("play", "undo");
    setNode(parent);
    app_.sessionChanged();
    if (!humanToMove()) requestEngineMove();
}

bool PlayScreen::claimDraw() {
    if (!canClaimDraw()) return false;
    const bool three = canClaimThreefold(pos_, tree_.keyHistoryTo(node_));
    finish("1/2-1/2", three ? "Draw by threefold repetition" : "Draw by the 50-move rule");
    return true;
}

void PlayScreen::resign() {
    if (finished_) return;
    finish(setup_.human == White ? "0-1" : "1-0", "You resigned");
}

void PlayScreen::finish(const std::string& result, const std::string& reason) {
    cancelEngine();
    finished_ = true;
    result_ = result;
    reason_ = reason;
    selected_ = NoSquare;
    util::Log::info("play", "game over: " + reason + " " + result);
    refreshBoard(RefreshHint::Content);
    refreshPanel();
    saveGame();
    app_.sessionChanged();  // a finished game has nothing to resume
    showResult();
}

std::string PlayScreen::pgn() const {
    PgnGame g;
    g.tree = GameTree(tree_.startPosition());
    GameNode* n = g.tree.root();
    for (Move m : tree_.mainLine()) n = g.tree.addMove(n, m);
    const util::CivilTime d = util::civilFromUnix(startedAt_);
    char date[16];
    std::snprintf(date, sizeof date, "%04d.%02d.%02d", d.year, d.month, d.day);
    const std::string engine = (engineName_.empty() ? std::string("Engine") : engineName_) + " (" + setup_.strength + ")";
    g.setHeader("Event", "PocketChess game");
    g.setHeader("Site", "PocketBook");
    g.setHeader("Date", date);
    g.setHeader("Round", "-");
    g.setHeader("White", setup_.human == White ? "You" : engine);
    g.setHeader("Black", setup_.human == Black ? "You" : engine);
    if (toFen(tree_.startPosition()) != toFen(Position::startPosition())) {
        g.setHeader("SetUp", "1");
        g.setHeader("FEN", toFen(tree_.startPosition()));
    }
    if (finished_) g.setHeader("Termination", reason_);
    g.result = result_;
    return writePgn(g);
}

void PlayScreen::saveGame() {
    storage::Database* db = app_.userDb();
    if (!db) return;
    storage::PlayedGame g;
    g.date = startedAt_;
    g.pgn = pgn();
    g.result = result_;
    g.strength = setup_.strength;
    g.human = setup_.human == White ? "white" : "black";
    if (const engine::EngineDescriptor* d = app_.engines().engineFor(engine::Role::Play)) g.engine = d->id;
    if (auto id = storage::addPlayedGame(*db, g); id) {
        savedId_ = *id;
        util::Log::info("play", "saved as played game " + std::to_string(savedId_));
    } else {
        util::Log::error("play", "saving the game: " + id.error().message());
    }
}

void PlayScreen::showResult() {
    const std::string w = winner(result_);
    const std::string human = setup_.human == White ? "White" : "Black";
    const std::string title = w.empty() ? "Draw" : (w == human ? "You won" : "You lost");
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, title, reason_ + " · " + (result_ == "1/2-1/2" ? "½-½" : result_),
        std::vector<std::string>{"Analyze", "Export PGN", "New game", "Close"},
        [this, alive](int i) {
            if (alive.expired()) return;
            switch (i) {
            case 0: analyzeGame(); break;
            case 1: exportPgn(); break;
            case 2: newGame(); break;
            default: break;
            }
        },
        true));
}

void PlayScreen::exportPgn() {
    const util::CivilTime d = util::civilFromUnix(startedAt_);
    char name[64];
    std::snprintf(name, sizeof name, "pocketchess-%04d%02d%02d-%02d%02d%02d.pgn", d.year, d.month, d.day, d.hour,
                  d.minute, d.second);
    const std::filesystem::path file = app_.paths().exports() / name;
    const std::string text = pgn();
    if (auto r = writeFile(file, std::vector<uint8_t>(text.begin(), text.end())); !r) {
        app_.showMessage("Export failed", r.error().message());
        return;
    }
    util::Log::info("play", "exported " + file.string());
    app_.showMessage("Exported", std::string("exports/") + name);
}

void PlayScreen::analyzeGame() {
    GameTree copy(tree_.startPosition());
    GameNode* n = copy.root();
    for (Move m : tree_.mainLine()) n = copy.addMove(n, m);
    app_.replaceTop(std::make_unique<AnalyzeScreen>(app_, std::move(copy)));
}

void PlayScreen::newGame() {
    PlaySetup s = setup_;
    app_.replaceTop(std::make_unique<PlayScreen>(app_, s));
}

void PlayScreen::openMenu() {
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, "Game", "", std::vector<std::string>{"New game", "Analyze game", backMenuLabel(app_, *this), "Close"},
        [this, alive](int i) {
            if (alive.expired()) return;
            switch (i) {
            case 0: newGame(); break;
            case 1: analyzeGame(); break;
            case 2: leave(); break;
            default: break;
            }
        },
        true));
}

void PlayScreen::leave() {
    if (finished_ || node_->isRoot()) {
        app_.pop();
        return;
    }
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(app_, "Leave this game?", "The game in progress is not saved.",
                                              std::vector<std::string>{"Leave", "Stay"}, [this, alive](int i) {
                                                  if (!alive.expired() && i == 0) app_.pop();
                                              }));
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool PlayScreen::saveSession(std::map<std::string, std::string>& out) const {
    if (finished_) return false;  // finished games live in played_games
    std::string moves;
    for (Move m : tree_.mainLine()) moves += (moves.empty() ? "" : " ") + toUci(m);
    const int plies = static_cast<int>(tree_.mainLine().size());
    out["play.human"] = setup_.human == White ? "white" : "black";
    out["play.strength"] = setup_.strength;
    out["play.fen"] = toFen(tree_.startPosition());
    out["play.moves"] = moves;
    out["play.started"] = std::to_string(startedAt_);
    out["play.flipped"] = flipped_ ? "true" : "false";
    out["summary"] = "Game in progress: you play " + std::string(setup_.human == White ? "White" : "Black") + " vs " +
                     (engineName_.empty() ? std::string("the engine") : engineName_) + " (" +
                     capitalized(setup_.strength) + "), " + std::to_string(plies) + (plies == 1 ? " move" : " moves") +
                     " played.";
    return true;
}

std::unique_ptr<PlayScreen> PlayScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto get = [&](const char* k) {
        auto it = s.find(k);
        return it == s.end() ? std::string() : it->second;
    };
    PlaySetup setup;
    const std::string human = get("play.human");
    if (human != "white" && human != "black") return nullptr;
    setup.human = human == "white" ? White : Black;
    setup.strength = get("play.strength");
    setup.fen = get("play.fen");
    if (setup.strength.empty() || !parseFen(setup.fen)) return nullptr;
    auto p = std::make_unique<PlayScreen>(app, setup);
    GameNode* n = p->tree_.root();
    Position pos = p->tree_.startPosition();
    const std::string moves = get("play.moves");
    size_t i = 0;
    while (i < moves.size()) {
        size_t j = moves.find(' ', i);
        if (j == std::string::npos) j = moves.size();
        auto m = parseUci(pos, std::string_view(moves).substr(i, j - i));
        if (!m) {
            util::Log::warn("play", "session: " + m.error().message());
            return nullptr;
        }
        pos.makeMove(m.value());
        n = p->tree_.addMove(n, m.value());
        i = j + 1;
    }
    const std::string started = get("play.started");
    std::from_chars(started.data(), started.data() + started.size(), p->startedAt_);  // keeps "now" if invalid
    p->flipped_ = get("play.flipped") == "true";
    p->setNode(n);
    if (gameOutcome(p->pos_, p->tree_.keyHistoryTo(n)).over()) return nullptr;  // never saved as unfinished
    util::Log::info("play", "resumed after " + std::to_string(p->tree_.mainLine().size()) + " plies");
    return p;
}

// --- Input and lifecycle -------------------------------------------------------------------------

void PlayScreen::onGesture(const Gesture& g) {
    if (g.type == Gesture::Type::Drag) {
        if (!app_.settings().dragToMove || !humanToMove()) return;
        const Square from = board_.squareAt(g.start);
        const Square to = board_.squareAt(g.end);
        if (from != NoSquare && to != NoSquare) {
            const Piece p = pos_.pieceAt(from);
            if (p != NoPiece && colorOf(p) == pos_.sideToMove()) tryMove(from, to);
        }
        return;
    }
    if (g.type == Gesture::Type::LongPress) {
        if (app_.layout().board.contains(g.start)) openMenu();
        return;
    }
    if (statusBackRect(app_).contains(g.start)) return leave();
    if (app_.layout().board.contains(g.start)) {
        if (app_.settings().tapToMove) onSquare(board_.squareAt(g.start));
        return;
    }
    switch (hitTest(buttons_, g.start)) {
    case BtnUndo: undo(); break;
    case BtnFlip:
        flipped_ = !flipped_;
        refreshBoard(RefreshHint::Content);
        app_.sessionChanged();
        break;
    case BtnDraw: claimDraw(); break;
    case BtnResign: {
        std::weak_ptr<char> alive = alive_;
        app_.push(std::make_unique<MessageDialog>(app_, "Resign?", "", std::vector<std::string>{"Resign", "Continue"},
                                                  [this, alive](int i) {
                                                      if (!alive.expired() && i == 0) resign();
                                                  }));
        break;
    }
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void PlayScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: undo(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: leave(); break;
    default: break;
    }
}

void PlayScreen::onEnter() {
    // First show, or back from a dialog/Settings: the engine continues if it is its turn.
    if (!humanToMove() && !job_ && !engineBlocked_) requestEngineMove();
}

void PlayScreen::onLeave() {
    // Covered by a dialog: keep thinking (the reply arrives underneath). Popped: the destructor
    // cancels the job.
}

void PlayScreen::onLifecycle(platform::Lifecycle l) {
    if (l == platform::Lifecycle::Background || l == platform::Lifecycle::Sleep || l == platform::Lifecycle::Exit) {
        cancelEngine();  // the manager also quits the process; onEnter/Foreground asks again
        app_.invalidate(statusRect_, RefreshHint::Content);
    } else if (l == platform::Lifecycle::Foreground || l == platform::Lifecycle::Show) {
        if (!humanToMove() && !job_ && !engineBlocked_ && app_.top() == this) requestEngineMove();
    }
}

} // namespace pchess::app
