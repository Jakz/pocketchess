// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/EndgameScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/Fen.h"
#include "core/chess/MoveGen.h"
#include "core/chess/San.h"
#include "core/util/Files.h"
#include "core/util/Log.h"

#include <charconv>

namespace pchess::app {

using namespace ui;
using namespace chess;
using games::EndgameJudge;
using games::EndgameTask;

namespace {

const char* goalText(const EndgameTask& t) {
    switch (t.goal) {
    case EndgameTask::Goal::Mate: return "Checkmate";
    case EndgameTask::Goal::Win: return "Win";
    case EndgameTask::Goal::Draw: return "Hold the draw";
    }
    return "";
}

std::optional<int> scoreCp(const engine::JobResult& r) {
    if (!r.score) return std::nullopt;
    if (r.score->kind == engine::Score::Kind::Mate) return r.score->value > 0 ? 100000 : -100000;
    return r.score->value;
}

} // namespace

EndgameScreen::EndgameScreen(Application& app, EndgameTask task, std::vector<Move> moves, int64_t attemptId)
    : Screen(app), task_(std::move(task)), judge_(task_) {
    board_.setRect(app_.layout().board);
    layoutPanel();
    start_ = parseFen(task_.fen).value();  // checked by parseEndgames
    flipped_ = task_.side == Black;
    moves_ = std::move(moves);
    for (size_t i = 0; i < moves_.size(); ++i) {
        // A resumed attempt: whether the user promoted.
        Position p = start_;
        for (size_t k = 0; k < i; ++k) p.makeMove(moves_[k]);
        if (p.sideToMove() == task_.side && moves_[i].isPromotion()) promoted_ = true;
    }
    replay();
    attempt_.endgame = task_.id;
    attempt_.started = attempt_.updated = app_.now();
    if (storage::Database* db = app_.userDb()) {
        if (attemptId) {
            auto s = db->prepare("SELECT started, hints, undos, moves FROM endgame_attempts WHERE id = ?1 AND result = -1");
            if (s) {
                s->bind(1, attemptId);
                if (auto row = s->step(); row && *row) {
                    attempt_.id = attemptId;
                    attempt_.started = s->int64(0);
                    attempt_.hints = s->integer(1);
                    attempt_.undos = s->integer(2);
                    attempt_.moves = s->integer(3);
                }
            }
        }
        if (!attempt_.id) save();
    }
    util::Log::info("endgame", task_.id + " attempt " + std::to_string(attempt_.id));
    setStatus(std::string(goalText(task_)) + " as " + (task_.side == White ? "White" : "Black"), task_.advice);
    refreshBoard(RefreshHint::Content);
}

EndgameScreen::~EndgameScreen() {
    alive_.reset();
    cancelJob();
}

void EndgameScreen::layoutPanel() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect p = l.panel;
    const int btnH = std::min(kMinTouchPx, p.h / 3);
    const int statusH = app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() + pad;
    statusRect_ = {p.x + pad * 2, p.y + pad, p.w - pad * 4, statusH};
    buttonsRect_ = {p.x + pad, p.bottom() - pad - btnH, p.w - 2 * pad, btnH};
    infoRect_ = {statusRect_.x, statusRect_.bottom(), statusRect_.w, buttonsRect_.y - pad - statusRect_.bottom()};
    const char* labels[] = {"◀", "▶", "Menu"};
    const char* hints[] = {"take back", "hint", ""};
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

void EndgameScreen::replay() {
    pos_ = start_;
    keys_.clear();
    for (Move m : moves_) {
        keys_.push_back(pos_.key());
        pos_.makeMove(m);
    }
}

bool EndgameScreen::userToMove() const { return !over() && pos_.sideToMove() == task_.side; }

// --- Playing -------------------------------------------------------------------------------------

void EndgameScreen::onEnter() {
    if (!over() && !userToMove() && !job_) engineMove();
}

void EndgameScreen::onLifecycle(platform::Lifecycle l) {
    if (l == platform::Lifecycle::Background || l == platform::Lifecycle::Sleep || l == platform::Lifecycle::Exit) {
        cancelJob();
    } else if ((l == platform::Lifecycle::Foreground || l == platform::Lifecycle::Show) && app_.top() == this) {
        onEnter();
    }
}

void EndgameScreen::cancelJob() {
    if (!job_) return;
    const engine::JobId id = job_;
    job_ = 0;  // its result is ignored from now on
    app_.engines().cancel(id);
}

bool EndgameScreen::playUci(const std::string& uci) {
    if (!userToMove() || job_) return false;
    auto m = parseUci(pos_, uci);
    if (!m) return false;
    commit(m.value());
    return true;
}

void EndgameScreen::commit(Move m) {
    selected_ = hintSquare_ = NoSquare;
    if (m.isPromotion()) promoted_ = true;
    moves_.push_back(m);
    keys_.push_back(pos_.key());
    pos_.makeMove(m);
    ++attempt_.moves;
    refreshBoard(RefreshHint::Content);
    afterMove(std::nullopt);
}

void EndgameScreen::afterMove(std::optional<int> engineScore) {
    const auto r = judge_.judge(pos_, keys_, engineScore, promoted_, attempt_.moves);
    if (r.verdict != EndgameJudge::Verdict::Continue) return finish(r);
    save();
    app_.sessionChanged();
    if (!userToMove()) engineMove();
    else setStatus("Your move", info_);
}

void EndgameScreen::engineMove() {
    if (!app_.engines().available(engine::Role::Play)) {
        setStatus("Engine missing", "Endgame practice needs an engine (see Diagnostics).");
        return;
    }
    std::weak_ptr<char> alive = alive_;
    setStatus("Engine thinking…", info_);
    job_ = app_.engines().bestMove(start_, moves_, "maximum", [this, alive](const engine::JobResult& r) {
        if (alive.expired() || r.id != job_) return;  // cancelled (take back, leave) or superseded
        job_ = 0;
        if (r.cancelled) return;
        if (!r.ok || r.best.isNone()) {
            setStatus("Engine failed", r.error.empty() ? "The engine returned no move." : r.error);
            return;
        }
        const Position before = pos_;
        moves_.push_back(r.best);
        keys_.push_back(pos_.key());
        pos_.makeMove(r.best);
        info_ = (task_.side == White ? "Black" : "White") + std::string(" played ") +
                std::to_string(before.fullmoveNumber()) + (before.sideToMove() == White ? ". " : "… ") +
                toSan(before, r.best) + ".";
        refreshBoard(RefreshHint::Content);
        afterMove(scoreCp(r));
    });
    updateButtons();
}

void EndgameScreen::finish(const EndgameJudge::Result& r) {
    cancelJob();
    result_ = r.verdict;
    attempt_.result = r.verdict == EndgameJudge::Verdict::Solved ? 1 : 0;
    attempt_.reason = r.reason;
    save();
    app_.sessionChanged();
    std::string info = r.reason;
    if (r.verdict == EndgameJudge::Verdict::Solved)
        info += " " + std::to_string(attempt_.moves) + " moves" +
                (attempt_.hints || attempt_.undos ? " (with " + std::to_string(attempt_.hints) + " hints, " +
                                                        std::to_string(attempt_.undos) + " take-backs)."
                                                  : ", on your own.");
    info += " Menu → Restart to try again.";
    util::Log::info("endgame", task_.id + (attempt_.result ? " solved: " : " failed: ") + r.reason);
    setStatus(r.verdict == EndgameJudge::Verdict::Solved ? "✓ Solved" : "✗ Not this time", info);
    refreshBoard(RefreshHint::Content);
}

void EndgameScreen::takeBack() {
    if (over() || moves_.empty()) return;
    cancelJob();
    // Back to the user's previous turn: the engine's reply and the user's move.
    const size_t before = moves_.size();
    do {
        moves_.pop_back();
    } while (!moves_.empty() && [&] {
        Position p = start_;
        for (Move m : moves_) p.makeMove(m);
        return p.sideToMove() != task_.side;
    }());
    if (moves_.size() == before) return;
    replay();
    if (pos_.sideToMove() != task_.side) {  // the engine moved first: nothing of the user's to take back
        engineMove();
        return;
    }
    ++attempt_.undos;
    attempt_.moves = std::max(0, attempt_.moves - 1);
    save();
    app_.sessionChanged();
    selected_ = hintSquare_ = NoSquare;
    setStatus("Taken back · your move", task_.advice);
    refreshBoard(RefreshHint::Content);
}

void EndgameScreen::hint() {
    if (!userToMove() || job_) return;
    if (!app_.engines().available(engine::Role::Hint)) return;
    std::weak_ptr<char> alive = alive_;
    setStatus("Engine thinking…", info_);
    job_ = app_.engines().hint(start_, moves_, [this, alive](const engine::JobResult& r) {
        if (alive.expired() || r.id != job_) return;
        job_ = 0;
        if (r.cancelled || !r.ok || r.best.isNone()) {
            setStatus("Your move", info_);
            return;
        }
        ++attempt_.hints;
        save();
        hintSquare_ = r.best.from();
        setStatus("Hint: " + toSan(pos_, r.best), info_);
        refreshBoard(RefreshHint::Selection);
    });
    updateButtons();
}

void EndgameScreen::save() {
    storage::Database* db = app_.userDb();
    if (!db) return;
    attempt_.updated = app_.now();
    if (auto id = storage::saveEndgameAttempt(*db, attempt_); id) attempt_.id = *id;
    else util::Log::error("endgame", "save: " + id.error().message());
}

// --- Drawing -------------------------------------------------------------------------------------

void EndgameScreen::setStatus(std::string status, std::string info) {
    status_ = std::move(status);
    info_ = std::move(info);
    app_.invalidate(statusRect_, RefreshHint::Content);
    app_.invalidate(infoRect_, RefreshHint::Content);
    updateButtons();
}

void EndgameScreen::refreshBoard(RefreshHint hint) {
    BoardVisual v = BoardVisual::fromPosition(pos_, moves_.empty() ? Move::none() : moves_.back());
    if (selected_ != NoSquare) {
        v.selected = selected_;
        if (app_.settings().showLegalMoves) {
            MoveList legal;
            generateLegalMoves(pos_, legal);
            for (Move m : legal)
                if (m.from() == selected_) v.targets |= bb(m.to());
        }
    } else if (hintSquare_ != NoSquare) {
        v.selected = hintSquare_;
    }
    v.flipped = flipped_;
    for (const Rect& r : board_.update(v)) app_.invalidateExact(r, hint);
    updateButtons();
}

void EndgameScreen::updateButtons() {
    for (Button& b : buttons_) {
        bool en = b.enabled;
        switch (b.id) {
        case BtnBack: en = !over() && !moves_.empty() && attempt_.moves > 0; break;
        case BtnHint: en = userToMove() && !job_ && hintSquare_ == NoSquare; break;
        default: break;
        }
        if (en != b.enabled) {
            b.enabled = en;
            app_.invalidate(b.rect, RefreshHint::Content);
        }
    }
}

void EndgameScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    drawStatusBar(app_, c, *this, task_.title, batteryText(app_));
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());
    if (clip.intersects(statusRect_)) {
        Font& f = app_.fonts().get(s.fontPx, FontStyle::Bold);
        c.pushClip(statusRect_);
        f.draw(c, statusRect_.x, statusRect_.y + f.ascent() + s.fontPx / 6, fitText(f, status_, statusRect_.w), t.ink);
        c.popClip();
    }
    if (clip.intersects(infoRect_)) {
        Font& small = app_.fonts().get(s.smallPx);
        auto lines = small.wrap(info_, infoRect_.w);
        const size_t fit = static_cast<size_t>(std::max(1, infoRect_.h / small.lineHeight()));
        if (lines.size() > fit) {
            lines.resize(fit);
            lines.back() += "…";
        }
        drawLines(c, infoRect_, lines, small, blend(t.background, t.ink, 170));
    }
    drawBackButton(app_, c, *this, backRect_);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

// --- Input ---------------------------------------------------------------------------------------

void EndgameScreen::select(Square s) {
    if (s == selected_) return;
    selected_ = s;
    refreshBoard(RefreshHint::Selection);
}

void EndgameScreen::onSquare(Square s) {
    if (s == NoSquare || !userToMove() || job_) return;
    MoveList legal;
    generateLegalMoves(pos_, legal);
    if (selected_ != NoSquare) {
        if (s == selected_) return select(NoSquare);
        for (Move m : legal)
            if (m.from() == selected_ && m.to() == s) return tryMove(selected_, s);
    }
    const Piece p = pos_.pieceAt(s);
    bool hasMoves = false;
    for (Move m : legal) hasMoves |= m.from() == s;
    select(p != NoPiece && colorOf(p) == pos_.sideToMove() && hasMoves ? s : NoSquare);
}

void EndgameScreen::tryMove(Square from, Square to) {
    MoveList legal;
    generateLegalMoves(pos_, legal);
    std::vector<Move> matches;
    for (Move m : legal)
        if (m.from() == from && m.to() == to) matches.push_back(m);
    if (matches.empty()) return select(NoSquare);
    if (matches.size() == 1) return commit(matches.front());
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<PromotionDialog>(app_, pos_.sideToMove(), [this, alive, matches](PieceType t) {
        if (alive.expired()) return;
        if (t == NoPieceType) return select(NoSquare);
        for (Move m : matches)
            if (m.promotion() == t) return commit(m);
    }));
}

void EndgameScreen::onGesture(const Gesture& g) {
    if (g.type == Gesture::Type::Drag) {
        if (!app_.settings().dragToMove || !userToMove() || job_) return;
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
    case BtnBack: takeBack(); break;
    case BtnHint: hint(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void EndgameScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: takeBack(); break;
    case KeyCode::Next: case KeyCode::Right: hint(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

void EndgameScreen::openMenu() {
    enum Action { Restart, Advice, Analyze, Flip, Back };
    std::vector<std::string> labels = {"Restart", "Advice", "Analyze from here", "Flip board", backMenuLabel(app_, *this),
                                       "Close"};
    const std::vector<Action> actions = {Restart, Advice, Analyze, Flip, Back};
    std::weak_ptr<char> alive = alive_;
    app_.push(std::make_unique<MessageDialog>(
        app_, task_.title, "", labels,
        [this, alive, actions](int i) {
            if (alive.expired() || i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Restart:
                if (!over() && attempt_.moves > 0) {
                    attempt_.result = 0;  // an abandoned attempt counts as not solved
                    attempt_.reason = "Restarted.";
                    save();
                }
                app_.replaceTop(std::make_unique<EndgameScreen>(app_, task_));
                break;
            case Advice: app_.showMessage(task_.title, task_.advice); break;
            case Analyze: analyzeHere(); break;
            case Flip:
                flipped_ = !flipped_;
                refreshBoard(RefreshHint::Content);
                break;
            case Back: app_.pop(); break;
            }
        },
        true));
}

void EndgameScreen::analyzeHere() {
    GameTree tree(start_);
    GameNode* n = tree.root();
    for (Move m : moves_) n = tree.addMove(n, m);
    app_.push(std::make_unique<AnalyzeScreen>(app_, std::move(tree), moves_));
}

// --- Task list -----------------------------------------------------------------------------------

std::vector<EndgameTask> EndgameScreen::loadTasks(Application& app) {
    auto text = util::readTextFile(app.paths().assets() / "endgames" / "endgames.tsv", 1u << 20);
    if (!text) {
        util::Log::warn("endgame", text.error().message());
        return {};
    }
    int rejected = 0;
    auto tasks = games::parseEndgames(*text, &rejected);
    if (rejected) util::Log::warn("endgame", std::to_string(rejected) + " bad line(s) in endgames.tsv");
    return tasks;
}

void EndgameScreen::choose(Application& app) {
    auto tasks = std::make_shared<std::vector<EndgameTask>>(loadTasks(app));
    if (tasks->empty()) return app.showMessage("Endgames", "assets/endgames/endgames.tsv is missing or empty.");
    std::map<std::string, storage::EndgameRecord> records;
    if (storage::Database* db = app.userDb())
        if (auto r = storage::endgameRecords(*db); r) records = std::move(*r);
    std::vector<std::string> labels, hints;
    for (const EndgameTask& t : *tasks) {
        const auto it = records.find(t.id);
        std::string hint = t.category + " · " + goalText(t);
        if (it != records.end())
            hint += it->second.solved ? " · ✓ solved " + std::to_string(it->second.solved) + "×"
                                      : " · " + std::to_string(it->second.attempts) + " tries";
        labels.push_back((it != records.end() && it->second.solvedAlone ? "✓ " : "") + t.title);
        hints.push_back(hint);
    }
    chooseFromList(app, "Endgames", labels, [&app, tasks](int i) {
        if (i >= 0) app.push(std::make_unique<EndgameScreen>(app, (*tasks)[static_cast<size_t>(i)]));
    }, 6, hints);
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool EndgameScreen::saveSession(std::map<std::string, std::string>& out) const {
    if (over()) return false;
    std::string moves;
    for (Move m : moves_) moves += (moves.empty() ? "" : " ") + toUci(m);
    out["endgame.id"] = task_.id;
    out["endgame.moves"] = moves;
    out["endgame.attempt"] = std::to_string(attempt_.id);
    out["summary"] = "Endgame: " + task_.title + ".";
    return true;
}

std::unique_ptr<EndgameScreen> EndgameScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto get = [&](const char* k) {
        auto it = s.find(k);
        return it == s.end() ? std::string() : it->second;
    };
    for (EndgameTask& t : loadTasks(app)) {
        if (t.id != get("endgame.id")) continue;
        Position p = parseFen(t.fen).value();
        std::vector<Move> moves;
        const std::string text = get("endgame.moves");
        size_t i = 0;
        while (i < text.size()) {
            size_t j = text.find(' ', i);
            if (j == std::string::npos) j = text.size();
            auto m = parseUci(p, std::string_view(text).substr(i, j - i));
            if (!m) break;
            moves.push_back(m.value());
            p.makeMove(m.value());
            i = j + 1;
        }
        int64_t id = 0;
        const std::string idText = get("endgame.attempt");
        (void)std::from_chars(idText.data(), idText.data() + idText.size(), id);
        return std::make_unique<EndgameScreen>(app, std::move(t), std::move(moves), id);
    }
    return nullptr;
}

} // namespace pchess::app
