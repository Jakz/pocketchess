// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/TacticsScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/util/Log.h"

#include <algorithm>
#include <chrono>

namespace pchess::app {

using namespace ui;
using namespace chess;
using puzzles::PuzzleSession;
using namespace std::chrono_literals;

namespace {
constexpr auto kOpponentDelay = 600ms;  // time to see the opponent's move arrive (design notes §9.4)
constexpr auto kReplyDelay = 400ms;
constexpr auto kSolutionStep = 700ms;
constexpr auto kAutoNextDelay = 1500ms;
} // namespace

TacticsScreen::TacticsScreen(Application& app, Mode mode, const std::string& puzzleId)
    : Screen(app), mode_(mode), timer_(&app.scheduler()) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    if (puzzles::Progress* pr = app_.progress()) {
        if (auto seen = pr->seenIds(); seen) seen_ = std::move(*seen);
        if (mode_ == Mode::Review)
            queue_ = app_.dueInPack(500);  // reviews of this pack (other packs' reviews wait)
    }
    if (!puzzleId.empty() && load(puzzleId)) return;
    loadNext();
}

TacticsScreen::~TacticsScreen() { alive_.reset(); }

void TacticsScreen::layoutPanel() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect p = l.panel;
    const int btnH = std::min(kMinTouchPx, p.h / 3);
    const int statusH = app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() + pad;
    statusRect_ = {p.x + pad * 2, p.y + pad, p.w - pad * 4, statusH};
    buttonsRect_ = {p.x + pad, p.bottom() - pad - btnH, p.w - 2 * pad, btnH};
    infoRect_ = {statusRect_.x, statusRect_.bottom(), statusRect_.w, buttonsRect_.y - pad - statusRect_.bottom()};
    const char* labels[] = {"◀", "▶", "Hint", "Solution", "Menu"};
    const char* hints[] = {"retry", "next", "", "", ""};
    const auto cells = splitRow(buttonsRect_, 6, pad);  // [‹] then the five buttons
    backRect_ = cells[0];
    for (int i = 0; i < 5; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        b.hint = hints[i];
        b.rect = cells[static_cast<size_t>(i) + 1];
        buttons_.push_back(b);
    }
}

// --- Choosing puzzles ----------------------------------------------------------------------------

bool TacticsScreen::load(const std::string& id) {
    puzzles::PuzzlePack* pack = app_.puzzlePack();
    if (!pack) return false;
    auto p = pack->byId(id);
    if (!p || !p->has_value()) return false;
    startSession(std::move(**p));
    return true;
}

void TacticsScreen::loadNext() {
    timer_.cancel();
    session_.reset();
    selected_ = hintSquare_ = NoSquare;
    puzzles::PuzzlePack* pack = app_.puzzlePack();
    if (!pack) {
        setStatus("No puzzle pack installed",
                  "Copy a puzzle pack (.db, made by PuzzleImporter) into imports/ and open Home → Import.");
        refreshBoard(RefreshHint::Content);
        return;
    }
    if (mode_ == Mode::Review) {
        while (!queue_.empty()) {
            const std::string id = queue_.front();
            queue_.erase(queue_.begin());
            if (load(id)) return;  // puzzles from another pack are skipped
        }
        setStatus("No reviews due", "Failed puzzles come back here on their review day.");
        refreshBoard(RefreshHint::Content);
        return;
    }
    const Settings& st = app_.settings();
    puzzles::SelectRequest req;
    req.rating = (app_.progress() ? app_.progress()->rating() : puzzles::kInitialRating) + st.tacticsDifficulty;
    if (!st.tacticsTheme.empty()) req.include = pack->themeMask({st.tacticsTheme});
    if (!st.tacticsSkipTheme.empty()) req.exclude = pack->themeMask({st.tacticsSkipTheme});
    req.skip = [this](const std::string& id) { return seen_.count(id) != 0; };
    puzzles::PuzzleSelector selector(*pack, app_.random());
    const auto t0 = std::chrono::steady_clock::now();
    auto p = selector.next(req);
    // M4 device budget: selection < 50 ms on the 50k pack (architecture doc §9).
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    util::Log::info("tactics", "selection " + std::to_string(static_cast<int>(ms + 0.5)) + " ms (rating " +
                                   std::to_string(req.rating) + ", " + std::to_string(seen_.size()) + " seen, pack " +
                                   pack->name() + ")");
    if (!p) {
        setStatus("Puzzle pack error", p.error().message());
    } else if (!p->has_value()) {
        setStatus("No more puzzles", "Every puzzle matching the filter has been played.");
    } else {
        startSession(std::move(**p));
        return;
    }
    refreshBoard(RefreshHint::Content);
}

void TacticsScreen::startSession(puzzles::Puzzle p) {
    const std::string id = p.id;
    session_.emplace(std::move(p));
    recorded_ = false;
    lastWrong_ = Move::none();
    if (!session_->valid()) {
        util::Log::warn("tactics", "puzzle " + id + " is invalid; skipped");
        seen_.insert(id);
        session_.reset();
        return loadNext();
    }
    flipped_ = session_->userColor() == Black;
    util::Log::info("tactics", "puzzle " + id + " (" + std::to_string(session_->puzzle().rating) + ")");
    setStatus(sideText() + " · opponent moves", "");
    refreshBoard(RefreshHint::Content);
    app_.sessionChanged();
    if (app_.settings().tacticsAutoOpponent) {
        std::weak_ptr<char> alive = alive_;
        timer_.start(kOpponentDelay, [this, alive] {
            if (!alive.expired()) afterOpponent();
        });
        updateButtons();  // Next stays off while the opponent's move is pending
    } else {
        setStatus(sideText() + " · tap Next for the opponent's move", "");
    }
}

// --- Playing -------------------------------------------------------------------------------------

std::string TacticsScreen::sideText() const {
    if (!session_) return "";
    return std::string(mode_ == Mode::Review ? "Review · " : "") + (session_->userColor() == White ? "White" : "Black");
}

void TacticsScreen::afterOpponent() {
    if (!session_ || session_->state() != PuzzleSession::State::OpponentToMove) return;
    session_->playOpponentMove();
    selected_ = hintSquare_ = NoSquare;
    refreshBoard(RefreshHint::Content);
    const bool first = session_->userMovesDone() == 0;
    setStatus(sideText() + (first ? " to move · find the best move" : " · keep going"),
              std::to_string(session_->userMovesDone()) + " of " + std::to_string(session_->userMovesTotal()) +
                  " moves");
}

bool TacticsScreen::playUci(const std::string& uci) {
    if (!session_ || session_->state() != PuzzleSession::State::AwaitUser) return false;
    auto m = parseUci(session_->position(), uci);
    if (!m) return false;
    commit(m.value());
    return true;
}

void TacticsScreen::commit(Move m) {
    selected_ = hintSquare_ = NoSquare;
    lastWrong_ = Move::none();
    const Position before = session_->position();
    const auto v = session_->tryMove(m);
    if (v == PuzzleSession::Verdict::Wrong) lastWrong_ = m;
    util::Log::debug("tactics", toSan(before, m) + (v == PuzzleSession::Verdict::Wrong ? " wrong" : " ok"));
    onVerdict(v);
}

void TacticsScreen::onVerdict(PuzzleSession::Verdict v) {
    refreshBoard(RefreshHint::Content);
    switch (v) {
    case PuzzleSession::Verdict::Correct: {
        setStatus("Correct", std::to_string(session_->userMovesDone()) + " of " +
                                 std::to_string(session_->userMovesTotal()) + " moves");
        std::weak_ptr<char> alive = alive_;
        timer_.start(kReplyDelay, [this, alive] {
            if (!alive.expired()) afterOpponent();
        });
        break;
    }
    case PuzzleSession::Verdict::Solved: finish(); break;
    case PuzzleSession::Verdict::Wrong:
        setStatus("Wrong move", "Retry, see the Solution, or Menu -> Check with engine.");
        break;
    case PuzzleSession::Verdict::NotYourTurn: break;
    }
}

void TacticsScreen::hint() {
    if (!session_) return;
    const Move m = session_->hint();
    if (m.isNone()) return;
    if (session_->state() == PuzzleSession::State::Wrong) session_->retry();
    hintSquare_ = m.from();
    setStatus("Hint: move the highlighted piece", "A hint counts as a miss for the rating.");
    refreshBoard(RefreshHint::Selection);
}

void TacticsScreen::retry() {
    if (!session_ || session_->state() != PuzzleSession::State::Wrong) return;
    session_->retry();
    setStatus(sideText() + " · try again", "");
    updateButtons();
}

void TacticsScreen::showSolution() {
    if (!session_ || session_->finished()) return;
    timer_.cancel();
    session_->giveUp();
    finish();
    stepSolution();
}

void TacticsScreen::stepSolution() {
    std::weak_ptr<char> alive = alive_;
    timer_.start(kSolutionStep, [this, alive] {
        if (alive.expired() || !session_) return;
        if (session_->nextSolutionMove()) {
            refreshBoard(RefreshHint::Content);
            stepSolution();
        }
    });
}

void TacticsScreen::finish() {
    if (recorded_ || !session_) return;
    recorded_ = true;
    const puzzles::Puzzle& p = session_->puzzle();
    const bool solved = session_->solvedClean();
    seen_.insert(p.id);
    std::string head = session_->state() == PuzzleSession::State::Solved ? (solved ? "Solved" : "Solved (with help)")
                                                                           : "Failed · solution";
    std::string rating;
    if (puzzles::Progress* pr = app_.progress()) {
        puzzles::PuzzlePack* pack = app_.puzzlePack();
        auto r = pr->record(pack ? pack->name() : "", p, solved, mode_ == Mode::Train, app_.now());
        if (r && mode_ == Mode::Train) {
            const int d = r->ratingAfter - r->ratingBefore;
            head += std::string(" · ") + (d >= 0 ? "+" : "") + std::to_string(d) + " → " + std::to_string(r->ratingAfter);
        } else if (r && r->state.nextReview) {
            head += " · again in " + std::to_string(r->state.intervalDays) + (r->state.intervalDays == 1 ? " day" : " days");
        }
        if (!r) util::Log::error("tactics", "progress: " + r.error().message());
    }
    std::string themes;
    if (puzzles::PuzzlePack* pack = app_.puzzlePack())
        for (const auto& t : pack->themesOf(p.themes)) themes += (themes.empty() ? "" : ", ") + t;
    rating = "Puzzle " + std::to_string(p.rating) + (themes.empty() ? "" : " · " + themes);
    setStatus(head, rating);
    app_.sessionChanged();
    if (app_.settings().tacticsAutoNext && session_->state() == PuzzleSession::State::Solved) {
        std::weak_ptr<char> alive = alive_;
        timer_.start(kAutoNextDelay, [this, alive] {
            if (!alive.expired()) loadNext();
        });
    }
}

void TacticsScreen::next() {
    if (session_ && session_->state() == PuzzleSession::State::OpponentToMove && !timer_.active()) {
        afterOpponent();  // "tap Next for the opponent's move"
        return;
    }
    if (session_ && !session_->finished()) return;
    loadNext();
}

// --- Drawing -------------------------------------------------------------------------------------

void TacticsScreen::setStatus(std::string status, std::string info) {
    status_ = std::move(status);
    info_ = std::move(info);
    refreshPanel();
}

void TacticsScreen::refreshBoard(RefreshHint hint) {
    BoardVisual v;
    if (session_) {
        const auto& played = session_->played();
        v = BoardVisual::fromPosition(session_->position(), played.empty() ? Move::none() : played.back());
        if (selected_ != NoSquare) {
            v.selected = selected_;
            if (app_.settings().showLegalMoves) {
                MoveList legal;
                generateLegalMoves(session_->position(), legal);
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

void TacticsScreen::refreshPanel() {
    app_.invalidate(statusRect_, RefreshHint::Content);
    app_.invalidate(infoRect_, RefreshHint::Content);
    updateButtons();
}

void TacticsScreen::updateButtons() {
    using S = PuzzleSession::State;
    const S st = session_ ? session_->state() : S::Failed;
    const bool playing = session_ && !session_->finished();
    for (Button& b : buttons_) {
        bool en = b.enabled;
        switch (b.id) {
        case BtnHint: en = session_ && (st == S::AwaitUser || st == S::Wrong); break;
        case BtnRetry: en = session_ && st == S::Wrong; break;
        case BtnSolution: en = playing; break;
        case BtnNext:
            en = !session_ ? app_.puzzlePack() != nullptr && (mode_ == Mode::Train || !queue_.empty())
                           : session_->finished() || (st == S::OpponentToMove && !timer_.active());
            break;
        default: break;
        }
        if (en != b.enabled) {
            b.enabled = en;
            app_.invalidate(b.rect, RefreshHint::Content);
        }
    }
}

void TacticsScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    const int rating = app_.progress() ? app_.progress()->rating() : puzzles::kInitialRating;
    const std::string title = mode_ == Mode::Review
                                  ? "Review · " + std::to_string(queue_.size() + (session_ ? 1 : 0)) + " left"
                                  : "Tactics · " + std::to_string(rating);
    drawStatusBar(app_, c, *this, title, batteryText(app_));
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());
    if (clip.intersects(statusRect_)) {
        Font& f = app_.fonts().get(s.fontPx, FontStyle::Bold);
        f.draw(c, statusRect_.x, statusRect_.y + f.ascent() + s.fontPx / 6, status_, t.ink);
    }
    if (clip.intersects(infoRect_)) {
        Font& small = app_.fonts().get(s.smallPx);
        drawLines(c, infoRect_, small.wrap(info_, infoRect_.w), small, blend(t.background, t.ink, 170));
    }
    drawBackButton(app_, c, *this, backRect_);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

// --- Input ---------------------------------------------------------------------------------------

void TacticsScreen::select(Square s) {
    if (s == selected_) return;
    selected_ = s;
    refreshBoard(RefreshHint::Selection);
}

void TacticsScreen::onSquare(Square s) {
    if (s == NoSquare || !session_ || session_->state() != PuzzleSession::State::AwaitUser) return;
    const Position& pos = session_->position();
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

void TacticsScreen::tryMove(Square from, Square to) {
    MoveList legal;
    generateLegalMoves(session_->position(), legal);
    std::vector<Move> matches;
    for (Move m : legal)
        if (m.from() == from && m.to() == to) matches.push_back(m);
    if (matches.empty()) return select(NoSquare);
    if (matches.size() == 1) return commit(matches.front());
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<PromotionDialog>(app_, session_->position().sideToMove(),
                                                [this, alive, matches](PieceType t) {
                                                    if (alive.expired()) return;
                                                    if (t == NoPieceType) return select(NoSquare);
                                                    for (Move m : matches)
                                                        if (m.promotion() == t) return commit(m);
                                                }));
}

void TacticsScreen::onGesture(const Gesture& g) {
    if (g.type == Gesture::Type::Drag) {
        if (!app_.settings().dragToMove || !session_ || session_->state() != PuzzleSession::State::AwaitUser) return;
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
    case BtnRetry: retry(); break;
    case BtnSolution: showSolution(); break;
    case BtnNext: next(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void TacticsScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Next: case KeyCode::Right: next(); break;  // hardware Next after completion
    case KeyCode::Prev: case KeyCode::Left: retry(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

void TacticsScreen::openMenu() {
    std::vector<std::string> labels;
    enum Action { Analyze, Check, Flip, Home };
    std::vector<Action> actions;
    if (session_) {
        labels.push_back("Analyze position");
        actions.push_back(Analyze);
    }
    if (session_ && !lastWrong_.isNone()) {
        labels.push_back("Check my move with engine");
        actions.push_back(Check);
    }
    labels.push_back("Flip board");
    actions.push_back(Flip);
    labels.push_back(backMenuLabel(app_, *this));
    actions.push_back(Home);
    labels.push_back("Close");
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, mode_ == Mode::Review ? "Review" : "Tactics", "", labels,
        [this, alive, actions](int i) {
            if (alive.expired() || i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Analyze: analyzePosition(); break;
            case Check: checkWithEngine(); break;
            case Flip:
                flipped_ = !flipped_;
                refreshBoard(RefreshHint::Content);
                break;
            case Home: app_.pop(); break;
            }
        },
        true));
}

void TacticsScreen::analyzePosition() {
    auto start = parseFen(session_->puzzle().fen);
    if (!start) return;
    GameTree tree(start.value());
    GameNode* n = tree.root();
    for (Move m : session_->played()) n = tree.addMove(n, m);
    app_.push(std::make_unique<AnalyzeScreen>(app_, std::move(tree)));
}

// "Check with engine" (design notes §9.4): both the user's move and the solution move are scored
// from the user's side; the rating does not change.
void TacticsScreen::checkWithEngine() {
    if (!session_ || lastWrong_.isNone()) return;
    if (!app_.engines().available(engine::Role::Validate)) {
        app_.showMessage("Engine missing", "Checking a move needs an engine (Settings / Diagnostics).");
        return;
    }
    auto start = parseFen(session_->puzzle().fen);
    if (!start) return;
    const std::vector<Move> moves = session_->played();
    const Position pos = session_->position();
    const Move mine = lastWrong_;
    const Move best = session_->puzzle().moves[moves.size()];
    std::weak_ptr<char> alive = alive_;
    setStatus("Engine: checking…", info_);
    app_.engines().validate(start.value(), moves, mine, [this, alive, start, moves, pos, mine, best](
                                                            const engine::JobResult& a) {
        if (alive.expired()) return;
        app_.engines().validate(start.value(), moves, best, [this, alive, pos, mine, best, a](const engine::JobResult& b) {
            if (alive.expired()) return;
            auto eval = [](const engine::JobResult& r) {
                return r.ok && r.score ? engine::formatScore(*r.score, true, false) : std::string("?");
            };
            const std::string text = "Your move " + toSan(pos, mine) + ": " + eval(a) + "\nSolution " +
                                     toSan(pos, best) + ": " + eval(b) + "\n(scores from your side)";
            setStatus("Wrong move", "Retry, see the Solution, or Menu -> Check with engine.");
            app_.showMessage("Engine check", text);
        });
    });
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool TacticsScreen::saveSession(std::map<std::string, std::string>& out) const {
    out["tactics.mode"] = mode_ == Mode::Review ? "review" : "train";
    // An unfinished puzzle starts again from its beginning; a finished one is not resumed.
    out["tactics.puzzle"] = session_ && !session_->finished() ? session_->puzzle().id : "";
    out["summary"] = mode_ == Mode::Review ? "Review session in progress." : "Tactics session in progress.";
    return true;
}

std::unique_ptr<TacticsScreen> TacticsScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto get = [&](const char* k) {
        auto it = s.find(k);
        return it == s.end() ? std::string() : it->second;
    };
    const std::string mode = get("tactics.mode");
    if (mode != "train" && mode != "review") return nullptr;
    return std::make_unique<TacticsScreen>(app, mode == "review" ? Mode::Review : Mode::Train, get("tactics.puzzle"));
}

} // namespace pchess::app
