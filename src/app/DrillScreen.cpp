// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/DrillScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/storage/ChessDb.h"
#include "core/util/Log.h"

#include <charconv>
#include <chrono>

namespace pchess::app {

using namespace ui;
using namespace chess;
using games::OpeningDrill;
using namespace std::chrono_literals;

namespace {

constexpr auto kReplyDelay = 500ms;

std::string uciList(const std::vector<Move>& moves) {
    std::string s;
    for (Move m : moves) s += (s.empty() ? "" : " ") + toUci(m);
    return s;
}

// UCI moves from `start`; stops at the first that does not parse.
std::vector<Move> parseUciList(const Position& start, const std::string& text) {
    std::vector<Move> out;
    Position p = start;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        auto m = parseUci(p, std::string_view(text).substr(i, j - i));
        if (!m) break;
        out.push_back(m.value());
        p.makeMove(m.value());
        i = j + 1;
    }
    return out;
}

int pct(int part, int whole) { return whole ? (part * 100 + whole / 2) / whole : 0; }

} // namespace

DrillScreen::DrillScreen(Application& app, std::vector<Move> start, Color user, std::vector<Move> played, int64_t runId)
    : Screen(app), start_(std::move(start)), user_(user), timer_(&app.scheduler()) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    flipped_ = user_ == Black;
    games::GamePack* pack = app_.bookPack();
    if (!pack) {
        setStatus("No game pack installed", "Opening practice uses a game pack's statistics: copy one into imports/.");
        return;
    }
    packName_ = pack->name();
    const auto meta = pack->meta();
    auto it = meta.find("title");
    packTitle_ = it != meta.end() && !it->second.empty() ? it->second : packName_;
    Position pos = Position::startPosition();
    for (Move m : start_) pos.makeMove(m);
    drill_.emplace(pos, user_, [pack](const Position& p) {
        auto moves = pack->bookMoves(p);
        return moves ? std::move(*moves) : std::vector<games::BookMove>{};
    }, app_.random());

    started_ = app_.now();
    storage::Database* db = app_.userDb();
    if (db && runId) {
        // Resume: the totals from the saved run, the moves from the session.
        auto s = db->prepare("SELECT started, main, book, missed FROM drill_runs WHERE id = ?1 AND finished = 0");
        if (s) {
            s->bind(1, runId);
            if (auto row = s->step(); row && *row) {
                runId_ = runId;
                started_ = s->int64(0);
                drill_->restoreScore({s->integer(1), s->integer(2), s->integer(3)});
            }
        }
        for (Move m : played) drill_->follow(m);
    }
    if (db && !runId_) {
        storage::DrillRun run;
        run.pack = packName_;
        run.side = user_ == White ? "white" : "black";
        run.start = uciList(start_);
        run.opening = openingName();
        run.started = run.updated = started_;
        if (auto id = storage::saveDrillRun(*db, run); id) runId_ = *id;
        else util::Log::error("drill", "save: " + id.error().message());
    }
    util::Log::info("drill", "run " + std::to_string(runId_) + " as " + (user_ == White ? "white" : "black") + " from '" +
                                 uciList(start_) + "', book " + packName_);
    if (drill_->finished()) finish();
    else if (!drill_->userToMove()) reply();
    else yourTurn("Your move", "");
}

DrillScreen::~DrillScreen() { alive_.reset(); }

void DrillScreen::layoutPanel() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect p = l.panel;
    const int btnH = std::min(kMinTouchPx, p.h / 3);
    const int statusH = app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() + pad;
    const int small = app_.fonts().get(s.smallPx).lineHeight();
    statusRect_ = {p.x + pad * 2, p.y + pad, p.w - pad * 4, statusH};
    buttonsRect_ = {p.x + pad, p.bottom() - pad - btnH, p.w - 2 * pad, btnH};
    scoreRect_ = {statusRect_.x, buttonsRect_.y - pad - small, statusRect_.w, small};
    infoRect_ = {statusRect_.x, statusRect_.bottom(), statusRect_.w, scoreRect_.y - pad - statusRect_.bottom()};
    const char* labels[] = {"◀", "▶", "Menu"};
    const char* hints[] = {"hint", "show move", ""};
    const auto cells = splitRow(buttonsRect_, 4, pad);  // [‹] ◀ ▶ Menu
    backRect_ = cells[0];
    for (int i = 0; i < 3; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        b.hint = hints[i];
        b.rect = cells[static_cast<size_t>(i) + 1];
        buttons_.push_back(b);
    }
}

// --- Texts ---------------------------------------------------------------------------------------

std::string DrillScreen::moveText(const Position& before, Move m) const {
    return std::to_string(before.fullmoveNumber()) + (before.sideToMove() == White ? ". " : "… ") + toSan(before, m);
}

std::string DrillScreen::openingName() const {
    const games::OpeningBook* book = app_.openings();
    if (!book) return "";
    std::vector<Move> path = start_;
    if (drill_) path.insert(path.end(), drill_->played().begin(), drill_->played().end());
    auto o = book->classify(Position::startPosition(), path);
    return o ? o->eco + " " + o->name : "";
}

std::string DrillScreen::scoreText() const {
    if (!drill_) return "";
    const OpeningDrill::Score& s = drill_->score();
    std::string t = std::to_string(s.main) + " main, " + std::to_string(s.book) + " other book, " +
                    std::to_string(s.missed) + " missed";
    if (!drill_->finished()) t += " · " + std::to_string(drill_->games()) + " games here";
    return t;
}

// --- Playing -------------------------------------------------------------------------------------

void DrillScreen::yourTurn(std::string status, std::string info) {
    info += (info.empty() ? "" : " ") + std::string("Which move do the players of ") + packTitle_ + " choose?";
    setStatus(std::move(status), std::move(info));
    refreshBoard(RefreshHint::Content);
    app_.sessionChanged();
}

bool DrillScreen::playUci(const std::string& uci) {
    if (!drill_ || !drill_->userToMove() || timer_.active()) return false;
    auto m = parseUci(drill_->position(), uci);
    if (!m) return false;
    commit(m.value());
    return true;
}

void DrillScreen::commit(Move m) {
    selected_ = NoSquare;
    const Position before = drill_->position();
    const bool hinted = drill_->hinted();
    // The book move actually played (the shown one when the second try misses too).
    const Move shown = drill_->candidates().empty() ? Move::none() : drill_->candidates().front().move;
    const auto v = drill_->guess(m);
    if (v == OpeningDrill::Verdict::NotYourTurn) return;
    if (v == OpeningDrill::Verdict::Wrong) {
        if (firstWrong_.empty()) firstWrong_ = toUci(m);
        setStatus("✗ " + toSan(before, m) + " is not in the book", "One more try, or ◀ for a hint, ▶ to see the move.");
        refreshBoard(RefreshHint::Selection);
        return;
    }
    if (v == OpeningDrill::Verdict::Shown && firstWrong_.empty()) firstWrong_ = toUci(m);
    const Move made = v == OpeningDrill::Verdict::Shown ? shown : m;
    hintSquare_ = NoSquare;
    persist(before, made, hinted);
    afterUserMove(v, before, made);
}

void DrillScreen::hint() {
    if (!drill_ || !drill_->userToMove() || timer_.active()) return;
    hintSquare_ = drill_->hint();
    selected_ = NoSquare;
    setStatus("Hint: the most played move moves this piece", "");
    refreshBoard(RefreshHint::Selection);
}

void DrillScreen::reveal() {
    if (!drill_ || !drill_->userToMove() || timer_.active()) return;
    const Position before = drill_->position();
    const bool hinted = drill_->hinted();
    const Move m = drill_->candidates().front().move;
    drill_->reveal();
    hintSquare_ = selected_ = NoSquare;
    persist(before, m, hinted);
    afterUserMove(OpeningDrill::Verdict::Shown, before, m);
}

void DrillScreen::afterUserMove(OpeningDrill::Verdict v, const Position& before, Move m) {
    const std::string share = " (" + std::to_string(drill_->lastSharePct()) + "%)";
    std::string status;
    if (v == OpeningDrill::Verdict::Main) status = "✓ " + moveText(before, m) + " · the main move" + share;
    else if (v == OpeningDrill::Verdict::Book) status = "✓ " + moveText(before, m) + " · a book move" + share;
    else status = "Book move: " + moveText(before, m) + share;
    setStatus(status, "");
    refreshBoard(RefreshHint::Content);
    app_.sessionChanged();
    if (drill_->finished()) return finish();
    std::weak_ptr<char> alive = alive_;
    timer_.start(kReplyDelay, [this, alive] {
        if (!alive.expired()) reply();
    });
    updateButtons();
}

void DrillScreen::reply() {
    const Position before = drill_->position();
    if (!drill_->opponentMove()) return finish();
    const Move m = drill_->played().back();
    std::string info = (before.sideToMove() == White ? "White" : "Black") + std::string(" played ") + moveText(before, m);
    // The reply's share at the position it was played in (looked up again: the drill moved on).
    if (games::GamePack* pack = app_.bookPack())
        if (auto moves = pack->bookMoves(before); moves) {
            int total = 0, games = 0;
            for (const auto& b : *moves) {
                total += b.games;
                if (b.move == m) games = b.games;
            }
            info += " (" + std::to_string(pct(games, total)) + "%)";
        }
    info += ".";
    if (drill_->finished()) {
        setStatus(status_, info);
        return finish();
    }
    yourTurn(status_.empty() ? "Your move" : status_, info);
}

void DrillScreen::persist(const Position& before, Move m, bool hinted) {
    storage::Database* db = app_.userDb();
    const std::string wrong = firstWrong_;
    firstWrong_.clear();
    if (!db || !runId_) return;
    storage::DrillMove dm;
    dm.ply = static_cast<int>(drill_->played().size()) - 1;
    dm.fen = toFen(before);
    dm.played = toUci(m);
    dm.outcome = static_cast<int>(drill_->lastOutcome());
    dm.share = drill_->lastSharePct();
    dm.wrong = wrong;
    dm.hinted = hinted;
    if (auto r = storage::addDrillMove(*db, runId_, dm); !r) util::Log::error("drill", "save: " + r.error().message());
    saveRun();
}

void DrillScreen::saveRun() {
    storage::Database* db = app_.userDb();
    if (!db || !runId_ || !drill_) return;
    storage::DrillRun run;
    run.id = runId_;
    run.pack = packName_;
    run.side = user_ == White ? "white" : "black";
    run.start = uciList(start_);
    run.opening = openingName();
    run.started = started_;
    run.updated = app_.now();
    run.moves = static_cast<int>(drill_->played().size());
    run.main = drill_->score().main;
    run.book = drill_->score().book;
    run.missed = drill_->score().missed;
    run.finished = drill_->finished();
    if (auto r = storage::saveDrillRun(*db, run); !r) util::Log::error("drill", "save: " + r.error().message());
}

void DrillScreen::finish() {
    timer_.cancel();
    hintSquare_ = selected_ = NoSquare;
    if (!recorded_) {
        recorded_ = true;
        saveRun();
    }
    const OpeningDrill::Score& s = drill_->score();
    std::string info = info_.empty() ? "" : info_ + " ";
    info += "Out of the book after " + std::to_string((static_cast<int>(start_.size() + drill_->played().size()) + 1) / 2) +
            " moves. Menu → Analyze from here to go on.";
    if (storage::Database* db = app_.userDb())
        if (auto st = storage::drillStats(*db); st && st->runs > 1) {
            const int all = st->main + st->book + st->missed;
            info += " All your drills: " + std::to_string(pct(st->main + st->book, all)) + "% of moves in the book.";
        }
    util::Log::info("drill", "run " + std::to_string(runId_) + " finished: " + std::to_string(s.main) + " main, " +
                                 std::to_string(s.book) + " book, " + std::to_string(s.missed) + " missed");
    setStatus("End of the book · " + std::to_string(s.main + s.book) + " of " + std::to_string(s.moves()) + " found",
              info);
    refreshBoard(RefreshHint::Content);
    app_.sessionChanged();
}

// --- Drawing -------------------------------------------------------------------------------------

void DrillScreen::setStatus(std::string status, std::string info) {
    status_ = std::move(status);
    info_ = std::move(info);
    app_.invalidate(statusRect_, RefreshHint::Content);
    app_.invalidate(infoRect_, RefreshHint::Content);
    app_.invalidate(scoreRect_, RefreshHint::Content);
    app_.invalidate(app_.layout().status, RefreshHint::Content);  // the opening's name
    updateButtons();
}

void DrillScreen::refreshBoard(RefreshHint hint) {
    BoardVisual v;
    if (drill_) {
        const auto& played = drill_->played();
        v = BoardVisual::fromPosition(drill_->position(), played.empty() ? (start_.empty() ? Move::none() : start_.back())
                                                                         : played.back());
        if (selected_ != NoSquare) {
            v.selected = selected_;
            if (app_.settings().showLegalMoves) {
                MoveList legal;
                generateLegalMoves(drill_->position(), legal);
                for (Move m : legal)
                    if (m.from() == selected_) v.targets |= bb(m.to());
            }
        } else if (hintSquare_ != NoSquare) {
            v.selected = hintSquare_;
        }
    }
    v.flipped = flipped_;
    for (const Rect& r : board_.update(v)) app_.invalidateExact(r, hint);
    updateButtons();
}

void DrillScreen::updateButtons() {
    const bool turn = drill_ && drill_->userToMove() && !timer_.active();
    for (Button& b : buttons_) {
        bool en = b.enabled;
        switch (b.id) {
        case BtnHint: en = turn && hintSquare_ == NoSquare; break;
        case BtnShow: en = turn; break;
        default: break;
        }
        if (en != b.enabled) {
            b.enabled = en;
            app_.invalidate(b.rect, RefreshHint::Content);
        }
    }
}

void DrillScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    const std::string name = openingName();
    drawStatusBar(app_, c, *this, name.empty() ? "Opening practice" : name, batteryText(app_));
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());
    if (clip.intersects(statusRect_)) {
        Font& f = app_.fonts().get(s.fontPx, FontStyle::Bold);
        c.pushClip(statusRect_);
        f.draw(c, statusRect_.x, statusRect_.y + f.ascent() + s.fontPx / 6, fitText(f, status_, statusRect_.w), t.ink);
        c.popClip();
    }
    Font& small = app_.fonts().get(s.smallPx);
    if (clip.intersects(infoRect_)) {
        auto lines = small.wrap(info_, infoRect_.w);
        const size_t fit = static_cast<size_t>(std::max(1, infoRect_.h / small.lineHeight()));
        if (lines.size() > fit) {
            lines.resize(fit);
            lines.back() += "…";
        }
        drawLines(c, infoRect_, lines, small, blend(t.background, t.ink, 170));
    }
    if (clip.intersects(scoreRect_)) {
        c.pushClip(scoreRect_);
        small.draw(c, scoreRect_.x, scoreRect_.y + small.ascent(), fitText(small, scoreText(), scoreRect_.w), t.ink);
        c.popClip();
    }
    drawBackButton(app_, c, *this, backRect_);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

// --- Input ---------------------------------------------------------------------------------------

void DrillScreen::select(Square s) {
    if (s == selected_) return;
    selected_ = s;
    refreshBoard(RefreshHint::Selection);
}

void DrillScreen::onSquare(Square s) {
    if (s == NoSquare || !drill_ || !drill_->userToMove() || timer_.active()) return;
    const Position& pos = drill_->position();
    MoveList legal;
    generateLegalMoves(pos, legal);
    if (selected_ != NoSquare) {
        if (s == selected_) return select(NoSquare);
        for (Move m : legal)
            if (m.from() == selected_ && m.to() == s) return tryMove(selected_, s);
    }
    const Piece p = pos.pieceAt(s);
    bool hasMoves = false;
    for (Move m : legal) hasMoves |= m.from() == s;
    select(p != NoPiece && colorOf(p) == pos.sideToMove() && hasMoves ? s : NoSquare);
}

void DrillScreen::tryMove(Square from, Square to) {
    MoveList legal;
    generateLegalMoves(drill_->position(), legal);
    std::vector<Move> matches;
    for (Move m : legal)
        if (m.from() == from && m.to() == to) matches.push_back(m);
    if (matches.empty()) return select(NoSquare);
    if (matches.size() == 1) return commit(matches.front());
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<PromotionDialog>(app_, drill_->position().sideToMove(),
                                                [this, alive, matches](PieceType t) {
                                                    if (alive.expired()) return;
                                                    if (t == NoPieceType) return select(NoSquare);
                                                    for (Move m : matches)
                                                        if (m.promotion() == t) return commit(m);
                                                }));
}

void DrillScreen::onGesture(const Gesture& g) {
    const bool turn = drill_ && drill_->userToMove() && !timer_.active();
    if (g.type == Gesture::Type::Drag) {
        if (!app_.settings().dragToMove || !turn) return;
        const Square from = board_.squareAt(g.start), to = board_.squareAt(g.end);
        if (from != NoSquare && to != NoSquare) tryMove(from, to);
        return;
    }
    if (g.type == Gesture::Type::LongPress) {
        if (app_.layout().board.contains(g.start)) openMenu();
        return;
    }
    if (backRect_.contains(g.start)) return app_.pop();
    if (app_.layout().board.contains(g.start)) {
        if (app_.settings().tapToMove) onSquare(board_.squareAt(g.start));
        return;
    }
    switch (hitTest(buttons_, g.start)) {
    case BtnHint: hint(); break;
    case BtnShow: reveal(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void DrillScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: hint(); break;
    case KeyCode::Next: case KeyCode::Right: reveal(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

void DrillScreen::openMenu() {
    enum Action { Analyze, Again, Flip, Back };
    std::vector<std::string> labels;
    std::vector<Action> actions;
    if (drill_) {
        labels.push_back("Analyze from here");
        actions.push_back(Analyze);
        labels.push_back("Practice again");
        actions.push_back(Again);
    }
    labels.push_back("Flip board");
    actions.push_back(Flip);
    labels.push_back(backMenuLabel(app_, *this));
    actions.push_back(Back);
    labels.push_back("Close");
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, "Opening practice", "", labels,
        [this, alive, actions](int i) {
            if (alive.expired() || i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Analyze: analyzeHere(); break;
            case Again: app_.replaceTop(std::make_unique<DrillScreen>(app_, start_, user_)); break;
            case Flip:
                flipped_ = !flipped_;
                refreshBoard(RefreshHint::Content);
                break;
            case Back: app_.pop(); break;
            }
        },
        true));
}

void DrillScreen::analyzeHere() {
    std::vector<Move> path = start_;
    path.insert(path.end(), drill_->played().begin(), drill_->played().end());
    GameTree tree;
    GameNode* n = tree.root();
    for (Move m : path) n = tree.addMove(n, m);
    app_.push(std::make_unique<AnalyzeScreen>(app_, std::move(tree), path));
}

void DrillScreen::choose(Application& app, std::vector<Move> start, const std::string& name) {
    chooseFromList(app, name.empty() ? "Opening practice" : name, {"Practice as White", "Practice as Black"},
                   [&app, start](int i) {
                       if (i < 0) return;
                       app.push(std::make_unique<DrillScreen>(app, start, i == 0 ? White : Black));
                   });
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool DrillScreen::saveSession(std::map<std::string, std::string>& out) const {
    if (!drill_ || !runId_ || drill_->finished()) return false;
    out["drill.run"] = std::to_string(runId_);
    out["drill.side"] = user_ == White ? "white" : "black";
    out["drill.start"] = uciList(start_);
    out["drill.played"] = uciList(drill_->played());
    out["summary"] = "Practising an opening.";
    return true;
}

std::unique_ptr<DrillScreen> DrillScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto get = [&](const char* k) {
        auto it = s.find(k);
        return it == s.end() ? std::string() : it->second;
    };
    int64_t id = 0;
    const std::string idText = get("drill.run");
    if (std::from_chars(idText.data(), idText.data() + idText.size(), id).ec != std::errc() || !id) return nullptr;
    const Color side = get("drill.side") == "black" ? Black : White;
    auto start = parseUciList(Position::startPosition(), get("drill.start"));
    Position pos = Position::startPosition();
    for (Move m : start) pos.makeMove(m);
    auto played = parseUciList(pos, get("drill.played"));
    auto screen = std::make_unique<DrillScreen>(app, std::move(start), side, std::move(played), id);
    return screen->ready() ? std::move(screen) : nullptr;
}

} // namespace pchess::app
