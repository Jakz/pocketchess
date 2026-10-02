// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/GuessScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "app/GameStudyScreen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/storage/ChessDb.h"
#include "core/util/Log.h"

#include <charconv>
#include <chrono>

namespace pchess::app {

using namespace ui;
using namespace chess;
using games::GuessSession;
using namespace std::chrono_literals;

namespace {

constexpr auto kReplyDelay = 500ms;  // time to see the user's move before the reply arrives
constexpr int kMinPlies = 20;        // random picks skip very short games

const char* sideKey(GuessSession::Side s) {
    return s == GuessSession::Side::White ? "white" : s == GuessSession::Side::Black ? "black" : "both";
}

std::optional<GuessSession::Side> sideFromKey(const std::string& k) {
    if (k == "white") return GuessSession::Side::White;
    if (k == "black") return GuessSession::Side::Black;
    if (k == "both") return GuessSession::Side::Both;
    return std::nullopt;
}

const char* outcomeText(GuessSession::Outcome o) {
    switch (o) {
    case GuessSession::Outcome::First: return "first try";
    case GuessSession::Outcome::Second: return "second try";
    case GuessSession::Outcome::Missed: return "shown";
    }
    return "";
}

} // namespace

GuessScreen::GuessScreen(Application& app, int64_t gameId, Side side, int fromPly, int64_t runId)
    : Screen(app), gameId_(gameId), timer_(&app.scheduler()) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    games::GamePack* pack = app_.gamePack();
    if (!pack) {
        error_ = "No game pack installed.";
    } else {
        pack_ = pack->name();
        auto game = pack->load(gameId);
        auto row = pack->row(gameId);
        if (!game || !row || !row->has_value()) {
            error_ = game ? "Game not found." : game.error().message();
        } else {
            game_ = std::move(*game);
            row_ = **row;
        }
    }
    if (!error_.empty()) {
        setStatus("Cannot start", error_);
        return;
    }
    for (const GameNode* n = game_.tree.root()->mainChild(); n; n = n->mainChild()) line_.push_back(n->move);

    // A saved run (resume) brings its score and position.
    GuessSession::Score score;
    started_ = app_.now();
    if (storage::Database* db = app_.userDb(); db && runId) {
        if (auto r = storage::guessRun(*db, runId); r && r->has_value()) {
            const storage::GuessRun& run = **r;
            runId_ = run.id;
            started_ = run.started;
            fromPly = run.ply;
            score = {run.first, run.second, run.missed};
            if (auto s = sideFromKey(run.side)) side = *s;
        }
    }
    session_.emplace(game_.tree.positionAt(game_.tree.root()), line_, side, fromPly, score);
    flipped_ = side == Side::Black;
    if (storage::Database* db = app_.userDb(); db && !runId_) {
        storage::GuessRun run;
        run.pack = pack_;
        run.gameId = gameId_;
        run.side = sideKey(side);
        run.title = row_.white + " – " + row_.black + (row_.date.size() >= 4 && row_.date[0] != '?' ? ", " + row_.date.substr(0, 4) : "");
        run.started = run.updated = started_;
        run.fromPly = run.ply = session_->ply();
        run.plies = session_->plies();
        if (auto id = storage::saveGuessRun(*db, run); id) runId_ = *id;
        else util::Log::error("guess", "save: " + id.error().message());
    }
    util::Log::info("guess", "game " + std::to_string(gameId_) + " of " + pack_ + " as " + sideKey(side) +
                                 " from ply " + std::to_string(session_->ply()) + " (run " + std::to_string(runId_) + ")");
    std::string replies;
    while (!session_->userToMove() && !session_->finished()) {  // the other side's moves up to the user's turn
        const Position before = session_->position();
        const Move m = session_->expected();
        session_->advance();
        replies = moveText(before, m);
    }
    if (session_->finished()) {
        finish();
        return;
    }
    yourTurn(session_->ply() == 0 ? "Your move" : "Your move · " + std::to_string(session_->position().fullmoveNumber()) +
                                                      (session_->position().sideToMove() == White ? "." : "…"),
             replies.empty() ? "" : "The game went on with " + replies + ".");
    app_.sessionChanged();
}

GuessScreen::~GuessScreen() { alive_.reset(); }

void GuessScreen::layoutPanel() {
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

std::string GuessScreen::player(Color c) const {
    const std::string& name = c == White ? row_.white : row_.black;
    const size_t comma = name.find(',');  // "Morphy, Paul" -> "Morphy"
    return name.empty() || name == "?" ? (c == White ? "White" : "Black") : name.substr(0, comma);
}

std::string GuessScreen::moveText(const Position& before, Move m) const {
    return std::to_string(before.fullmoveNumber()) + (before.sideToMove() == White ? ". " : "… ") + toSan(before, m);
}

std::string GuessScreen::commentAt(int ply) const {
    const GameNode* n = game_.tree.root();
    for (int i = 0; i < ply && n; ++i) n = n->mainChild();
    if (!n || n->isRoot()) return "";
    return n->preComment.empty() ? n->comment : n->preComment + " " + n->comment;
}

std::string GuessScreen::scoreText() const {
    if (!session_) return "";
    const GuessSession::Score& s = session_->score();
    std::string t = "Move " + std::to_string((session_->ply() + 2) / 2) + " of " + std::to_string((session_->plies() + 1) / 2);
    if (s.guessed())
        t += " · " + std::to_string(s.percent()) + "% · " + std::to_string(s.first) + " first, " +
             std::to_string(s.second) + " second, " + std::to_string(s.missed) + " shown";
    return t;
}

// --- Playing -------------------------------------------------------------------------------------

void GuessScreen::yourTurn(std::string status, std::string info) {
    const Color c = session_->position().sideToMove();
    info += (info.empty() ? "" : " ") + std::string("What did ") + player(c) + " play?";
    setStatus(std::move(status), std::move(info));
    refreshBoard(RefreshHint::Content);
}

bool GuessScreen::playUci(const std::string& uci) {
    if (!session_ || !session_->userToMove() || timer_.active()) return false;
    auto m = parseUci(session_->position(), uci);
    if (!m) return false;
    commit(m.value());
    return true;
}

void GuessScreen::commit(Move m) {
    selected_ = NoSquare;
    const Position before = session_->position();
    const int ply = session_->ply();
    const bool hinted = session_->hinted();
    const auto v = session_->guess(m);
    if (v == GuessSession::Verdict::NotYourTurn) return;
    if (v == GuessSession::Verdict::Wrong) {
        if (firstWrong_.empty()) firstWrong_ = toUci(m);
        setStatus("✗ " + toSan(before, m) + " is not the game move",
                  "One more try, or ◀ for a hint, ▶ to see the move.");
        refreshBoard(RefreshHint::Selection);
        return;
    }
    if (v == GuessSession::Verdict::Revealed && firstWrong_.empty()) firstWrong_ = toUci(m);
    hintSquare_ = NoSquare;
    persist(ply, session_->lastOutcome(), hinted);
    const std::string san = moveText(before, line_[static_cast<size_t>(ply)]);
    afterUserMove(v, san);
}

void GuessScreen::hint() {
    if (!session_ || !session_->userToMove() || timer_.active()) return;
    hintSquare_ = session_->hint();
    selected_ = NoSquare;
    setStatus("Hint: the highlighted piece moved", "A right guess now counts as a second try.");
    refreshBoard(RefreshHint::Selection);
}

void GuessScreen::reveal() {
    if (!session_ || !session_->userToMove() || timer_.active()) return;
    const Position before = session_->position();
    const int ply = session_->ply();
    const bool hinted = session_->hinted();
    session_->reveal();
    hintSquare_ = selected_ = NoSquare;
    persist(ply, GuessSession::Outcome::Missed, hinted);
    afterUserMove(GuessSession::Verdict::Revealed, moveText(before, line_[static_cast<size_t>(ply)]));
}

void GuessScreen::afterUserMove(GuessSession::Verdict v, const std::string& san) {
    const std::string status = v == GuessSession::Verdict::Match
                                   ? "✓ " + san + " · " + outcomeText(session_->lastOutcome())
                                   : "Game move: " + san;
    std::string info = commentAt(session_->ply());
    setStatus(status, info);
    refreshBoard(RefreshHint::Content);
    app_.sessionChanged();
    if (session_->finished()) return finish();
    if (session_->userToMove()) return yourTurn(status, info);  // both sides: straight on
    std::weak_ptr<char> alive = alive_;
    timer_.start(kReplyDelay, [this, alive] {
        if (!alive.expired()) reply();
    });
    updateButtons();
}

void GuessScreen::reply() {
    // The verdict stays on the status line; the info says what came back.
    const std::string status = status_;
    std::string info;
    while (session_ && !session_->userToMove() && !session_->finished()) {
        const Position before = session_->position();
        const Move m = session_->expected();
        session_->advance();
        info += (info.empty() ? "" : " ") + player(before.sideToMove()) + " replied " + moveText(before, m) + ".";
        if (const std::string c = commentAt(session_->ply()); !c.empty()) info += " " + c;
    }
    if (session_->finished()) {
        refreshBoard(RefreshHint::Content);
        return finish();
    }
    yourTurn(status, info);
}

void GuessScreen::persist(int ply, GuessSession::Outcome o, bool hinted) {
    storage::Database* db = app_.userDb();
    const std::string wrong = firstWrong_;
    firstWrong_.clear();
    if (!db || !runId_) return;
    storage::GuessRun run;
    run.id = runId_;
    run.pack = pack_;
    run.gameId = gameId_;
    run.side = sideKey(session_->side());
    run.title = row_.white + " – " + row_.black + (row_.date.size() >= 4 && row_.date[0] != '?' ? ", " + row_.date.substr(0, 4) : "");
    run.started = started_;
    run.updated = app_.now();
    run.ply = session_->ply();
    run.plies = session_->plies();
    run.first = session_->score().first;
    run.second = session_->score().second;
    run.missed = session_->score().missed;
    run.finished = session_->finished();
    if (auto r = storage::guessRun(*db, runId_); r && r->has_value()) run.fromPly = (*r)->fromPly;
    auto r = db->transaction([&]() -> Result<void> {
        if (auto a = storage::addGuessMove(*db, runId_, ply, static_cast<int>(o), wrong, hinted);
            !a)
            return a;
        if (auto s = storage::saveGuessRun(*db, run); !s) return s.error();
        return {};
    });
    if (!r) util::Log::error("guess", "save: " + r.error().message());
}

void GuessScreen::finish() {
    timer_.cancel();
    hintSquare_ = selected_ = NoSquare;
    if (storage::Database* db = app_.userDb(); db && runId_) {
        if (auto r = storage::guessRun(*db, runId_); r && r->has_value() && !(*r)->finished) {
            storage::GuessRun run = **r;
            run.finished = true;
            run.ply = session_->ply();
            run.updated = app_.now();
            if (auto s = storage::saveGuessRun(*db, run); !s) util::Log::error("guess", "save: " + s.error().message());
        }
    }
    const GuessSession::Score& s = session_->score();
    std::string info = std::to_string(s.first) + " first try, " + std::to_string(s.second) + " second, " +
                       std::to_string(s.missed) + " shown. Result " + (row_.result == "1/2-1/2" ? "½-½" : row_.result) + ".";
    if (storage::Database* db = app_.userDb())
        if (auto st = storage::guessStats(*db); st && st->finished > 1)
            info += " All your runs: " + std::to_string(st->percent()) + "% over " + std::to_string(st->finished) + " games.";
    util::Log::info("guess", "run " + std::to_string(runId_) + " finished: " + std::to_string(s.percent()) + "%");
    setStatus("Game over · " + std::to_string(s.percent()) + "%", info);
    refreshBoard(RefreshHint::Content);
    app_.sessionChanged();
}

// --- Drawing -------------------------------------------------------------------------------------

void GuessScreen::setStatus(std::string status, std::string info) {
    status_ = std::move(status);
    info_ = std::move(info);
    app_.invalidate(statusRect_, RefreshHint::Content);
    app_.invalidate(infoRect_, RefreshHint::Content);
    app_.invalidate(scoreRect_, RefreshHint::Content);
    updateButtons();
}

void GuessScreen::refreshBoard(RefreshHint hint) {
    BoardVisual v;
    if (session_) {
        v = BoardVisual::fromPosition(session_->position(), session_->lastMove());
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

void GuessScreen::updateButtons() {
    const bool turn = session_ && session_->userToMove() && !timer_.active();
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

void GuessScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    std::string title = "Guess the move";
    if (session_) title = player(White) + " – " + player(Black);
    drawStatusBar(app_, c, *this, title, batteryText(app_));
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

void GuessScreen::select(Square s) {
    if (s == selected_) return;
    selected_ = s;
    refreshBoard(RefreshHint::Selection);
}

void GuessScreen::onSquare(Square s) {
    if (s == NoSquare || !session_ || !session_->userToMove() || timer_.active()) return;
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

void GuessScreen::tryMove(Square from, Square to) {
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

void GuessScreen::onGesture(const Gesture& g) {
    const bool turn = session_ && session_->userToMove() && !timer_.active();
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

void GuessScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: hint(); break;
    case KeyCode::Next: case KeyCode::Right: reveal(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

void GuessScreen::openMenu() {
    enum Action { Study, Another, Flip, Back };
    std::vector<std::string> labels;
    std::vector<Action> actions;
    if (session_) {
        labels.push_back("Study this game");
        actions.push_back(Study);
    }
    labels.push_back("Guess another game");
    actions.push_back(Another);
    labels.push_back("Flip board");
    actions.push_back(Flip);
    labels.push_back(backMenuLabel(app_, *this));
    actions.push_back(Back);
    labels.push_back("Close");
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, "Guess the move", "", labels,
        [this, alive, actions](int i) {
            if (alive.expired() || i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Study: study(); break;
            case Another: another(); break;
            case Flip:
                flipped_ = !flipped_;
                refreshBoard(RefreshHint::Content);
                break;
            case Back: app_.pop(); break;
            }
        },
        true));
}

void GuessScreen::study() {
    // The study view at the same position (the moves already played are no secret any more).
    std::vector<std::string> path;
    for (int i = 0; i < session_->ply(); ++i) path.push_back(toUci(line_[static_cast<size_t>(i)]));
    app_.replaceTop(std::make_unique<GameStudyScreen>(app_, gameId_, path));
}

void GuessScreen::another() {
    const int64_t id = randomGame(app_, another_);
    if (!id) return app_.showMessage("Guess the move", "No other game matches.");
    const Side side = session_ ? session_->side() : Side::White;
    auto next = std::make_unique<GuessScreen>(app_, id, side, 0);
    next->another_ = another_;
    app_.replaceTop(std::move(next));
}

int64_t GuessScreen::randomGame(Application& app, const games::GameFilter& filter) {
    games::GamePack* pack = app.gamePack();
    if (!pack) return 0;
    const bool unfiltered = filter.player.empty() && filter.event.empty() && filter.eco.empty() &&
                            filter.opening.empty() && filter.result.empty() && !filter.ids && !filter.yearFrom &&
                            !filter.yearTo;
    int n = unfiltered ? pack->count() : 0;
    if (!unfiltered) {
        auto c = pack->count(filter);
        if (!c) return 0;
        n = std::min(*c, games::GamePack::kCountCap);
    }
    if (n <= 0) return 0;
    const auto t0 = std::chrono::steady_clock::now();
    int64_t pick = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {  // prefer games long enough to guess
        auto page = pack->page(filter, static_cast<int>(app.random().below(static_cast<uint32_t>(n))), 1);
        if (!page || page->empty()) break;
        pick = page->front().id;
        if (page->front().plies >= kMinPlies) break;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    util::Log::info("guess", "random game " + std::to_string(pick) + " of " + std::to_string(n) + " in " +
                                 std::to_string(static_cast<int>(ms + 0.5)) + " ms");
    return pick;
}

void GuessScreen::choose(Application& app, int64_t gameId, int fromPly, games::GameFilter another) {
    games::GamePack* pack = app.gamePack();
    if (!pack || !gameId) return;
    auto row = pack->row(gameId);
    if (!row || !row->has_value()) return;
    auto name = [](const std::string& n, const char* side) { return n.empty() || n == "?" ? std::string(side) : n; };
    const std::vector<std::string> labels = {"Play White (" + name((*row)->white, "White") + ")",
                                             "Play Black (" + name((*row)->black, "Black") + ")", "Both sides"};
    const std::string title = fromPly > 0 ? "Guess from move " + std::to_string(fromPly / 2 + 1) : "Guess the moves";
    chooseFromList(app, title, labels, [&app, gameId, fromPly, another](int i) {
        if (i < 0) return;
        const Side side = i == 0 ? Side::White : i == 1 ? Side::Black : Side::Both;
        auto screen = std::make_unique<GuessScreen>(app, gameId, side, fromPly);
        screen->another_ = another;
        app.push(std::move(screen));
    });
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool GuessScreen::saveSession(std::map<std::string, std::string>& out) const {
    if (!session_ || !runId_ || session_->finished()) return false;
    out["guess.run"] = std::to_string(runId_);
    out["summary"] = "Guessing the moves of " + row_.white + " – " + row_.black + ".";
    return true;
}

std::unique_ptr<GuessScreen> GuessScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto it = s.find("guess.run");
    if (it == s.end()) return nullptr;
    int64_t id = 0;
    if (std::from_chars(it->second.data(), it->second.data() + it->second.size(), id).ec != std::errc()) return nullptr;
    storage::Database* db = app.userDb();
    games::GamePack* pack = app.gamePack();
    if (!db || !pack) return nullptr;
    auto run = storage::guessRun(*db, id);
    if (!run || !run->has_value() || (*run)->finished || (*run)->pack != pack->name()) return nullptr;
    auto side = sideFromKey((*run)->side);
    if (!side) return nullptr;
    auto screen = std::make_unique<GuessScreen>(app, (*run)->gameId, *side, (*run)->ply, id);
    return screen->loaded() ? std::move(screen) : nullptr;
}

} // namespace pchess::app
