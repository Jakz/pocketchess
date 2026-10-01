// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/GameStudyScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/San.h"
#include "core/storage/ChessDb.h"
#include "core/util/Log.h"

#include <charconv>

namespace pchess::app {

using namespace ui;
using namespace chess;

GameStudyScreen::GameStudyScreen(Application& app, int64_t gameId, const std::vector<std::string>& path)
    : Screen(app), gameId_(gameId) {
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
            loaded_ = true;
        }
    }
    if (storage::Database* db = app_.userDb(); db && loaded_)
        if (auto fav = storage::isFavorite(*db, pack_, gameId_); fav) favorite_ = *fav;
    const GameNode* n = loaded_ ? game_.tree.root() : nullptr;
    if (n) {
        for (const std::string& uci : path) {
            const GameNode* next = nullptr;
            for (const auto& c : n->children)
                if (toUci(c->move) == uci) next = c.get();
            if (!next) break;
            n = next;
        }
    }
    setNode(n);
}

void GameStudyScreen::layoutPanel() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const Rect p = l.panel;
    const int btnH = std::min(kMinTouchPx, p.h / 3);
    const int small = app_.fonts().get(s.smallPx).lineHeight();
    headRect_ = {p.x + pad * 2, p.y + pad, p.w - pad * 4, small};
    commentRect_ = {headRect_.x, headRect_.bottom() + pad / 2, headRect_.w, 2 * small};
    buttonsRect_ = {p.x + pad, p.bottom() - pad - btnH, p.w - 2 * pad, btnH};
    movesRect_ = {headRect_.x, commentRect_.bottom() + pad / 2, headRect_.w, buttonsRect_.y - pad - commentRect_.bottom()};
    const char* labels[] = {"◀", "▶", "Main", "☆", "Menu"};
    const auto cells = splitRow(buttonsRect_, 6, pad);  // [‹] then the five buttons
    backRect_ = cells[0];
    for (int i = 0; i < 5; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        if (i < 2) b.hint = "move";  // the Prev/Next keys
        b.rect = cells[static_cast<size_t>(i) + 1];
        buttons_.push_back(b);
    }
}

// --- Navigation ----------------------------------------------------------------------------------

void GameStudyScreen::setNode(const GameNode* n) {
    node_ = n;
    pos_ = n ? game_.tree.positionAt(n) : Position::startPosition();
    refreshBoard();
    app_.invalidate(commentRect_, RefreshHint::Content);
    app_.invalidate(movesRect_, RefreshHint::Content);
    updateButtons();
    app_.sessionChanged();
}

void GameStudyScreen::next() {
    if (node_ && node_->mainChild()) setNode(node_->mainChild());
}

void GameStudyScreen::prev() {
    if (node_ && node_->parent) setNode(node_->parent);
}

void GameStudyScreen::mainLine() {
    const GameNode* n = node_;
    while (n && !n->isMainLine()) n = n->parent;
    if (n && n != node_) setNode(n);
}

void GameStudyScreen::jumpTo(const GameNode* n) {
    if (n && n != node_) setNode(n);
}

void GameStudyScreen::enterVariation(int index) {
    if (!node_ || index <= 0 || index >= static_cast<int>(node_->children.size())) return;
    setNode(node_->children[static_cast<size_t>(index)].get());
}

void GameStudyScreen::toggleFavorite() {
    storage::Database* db = app_.userDb();
    if (!loaded_) return;
    favorite_ = !favorite_;
    if (db)
        if (auto r = storage::setFavorite(*db, pack_, gameId_, favorite_, app_.now()); !r)
            util::Log::error("games", "favorite: " + r.error().message());
    updateButtons();
}

// --- Drawing -------------------------------------------------------------------------------------

std::string GameStudyScreen::headline() const {
    if (!loaded_) return error_;
    std::string s = row_.white + " – " + row_.black + " · " + (row_.result == "1/2-1/2" ? "½-½" : row_.result);
    if (row_.date.size() >= 4 && row_.date[0] != '?') s += " · " + row_.date.substr(0, 4);
    return s;
}

void GameStudyScreen::refreshBoard() {
    BoardVisual v = BoardVisual::fromPosition(pos_, node_ && !node_->isRoot() ? node_->move : Move::none());
    v.flipped = flipped_;
    for (const Rect& r : board_.update(v)) app_.invalidateExact(r, RefreshHint::Content);
}

void GameStudyScreen::updateButtons() {
    for (Button& b : buttons_) {
        bool en = b.enabled;
        bool primary = b.primary;
        std::string label = b.label;
        switch (b.id) {
        case BtnPrev: en = node_ && node_->parent; break;
        case BtnNext: en = node_ && node_->mainChild(); break;
        case BtnMain: en = node_ && !node_->isMainLine(); break;
        case BtnStar:
            en = loaded_;
            label = favorite_ ? "★" : "☆";
            primary = favorite_;
            break;
        default: break;
        }
        if (en != b.enabled || primary != b.primary || label != b.label) {
            b.enabled = en;
            b.primary = primary;
            b.label = label;
            app_.invalidate(b.rect, RefreshHint::Content);
        }
    }
}

void GameStudyScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    drawStatusBar(app_, c, *this, loaded_ && !row_.event.empty() ? row_.event : "Game", batteryText(app_));
    if (clip.intersects(l.board)) board_.draw(c, clip, t, app_.pieces(), app_.fonts());
    Font& small = app_.fonts().get(s.smallPx);
    Font& smallBold = app_.fonts().get(s.smallPx, FontStyle::Bold);
    if (clip.intersects(headRect_)) {
        c.pushClip(headRect_);
        smallBold.draw(c, headRect_.x, headRect_.y + smallBold.ascent(), fitText(smallBold, headline(), headRect_.w),
                       t.ink);
        c.popClip();
    }
    if (clip.intersects(commentRect_) && node_) {
        // Comments under the board (design notes §13.5): the current move's, or the game's.
        std::string text = node_->preComment.empty() ? node_->comment : node_->preComment + " " + node_->comment;
        if (text.empty() && node_->isRoot() && loaded_) text = row_.opening.empty() ? "" : row_.eco + " " + row_.opening;
        auto lines = small.wrap(text, commentRect_.w);
        if (lines.size() > 2) {
            lines.resize(2);
            lines.back() += "…";
        }
        drawLines(c, commentRect_, lines, small, blend(t.background, t.ink, 190));
    }
    if (clip.intersects(movesRect_) && loaded_)
        drawMoveList(app_, c, movesRect_, game_.tree, node_, "Next ▶ to step through the game.", &hits_);
    drawBackButton(app_, c, *this, backRect_);
    for (const Button& b : buttons_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

// --- Input ---------------------------------------------------------------------------------------

void GameStudyScreen::onGesture(const Gesture& g) {
    if (g.type == Gesture::Type::LongPress) return openMenu();
    if (g.type != Gesture::Type::Tap) return;
    if (backRect_.contains(g.start)) return app_.pop();
    for (const MoveHit& h : hits_)
        if (h.rect.contains(g.start)) return jumpTo(h.node);
    if (app_.layout().board.contains(g.start)) return next();  // tapping the board steps forward
    switch (hitTest(buttons_, g.start)) {
    case BtnPrev: prev(); break;
    case BtnNext: next(); break;
    case BtnMain: mainLine(); break;
    case BtnStar: toggleFavorite(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void GameStudyScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: prev(); break;
    case KeyCode::Next: case KeyCode::Right: next(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

void GameStudyScreen::openMenu() {
    enum Action { Variations, Analyze, Flip, Info, Back };
    std::vector<std::string> labels;
    std::vector<Action> actions;
    if (node_ && node_->children.size() > 1) {
        labels.push_back("Variations (" + std::to_string(node_->children.size() - 1) + ")…");
        actions.push_back(Variations);
    }
    if (loaded_) {
        labels.push_back("Analyze from here");
        actions.push_back(Analyze);
    }
    labels.push_back("Flip board");
    actions.push_back(Flip);
    if (loaded_) {
        labels.push_back("Game info");
        actions.push_back(Info);
    }
    labels.push_back(backMenuLabel(app_, *this));  // the reader's Back key never reaches the app
    actions.push_back(Back);
    labels.push_back("Close");
    app_.push(std::make_unique<MessageDialog>(
        app_, "Game", "", labels,
        [this, actions](int i) {
            if (i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Variations: chooseVariation(); break;
            case Analyze: analyzeHere(); break;
            case Flip:
                flipped_ = !flipped_;
                refreshBoard();
                break;
            case Info: showInfo(); break;
            case Back: app_.pop(); break;
            }
        },
        true));
}

void GameStudyScreen::chooseVariation() {
    std::vector<std::string> labels;
    for (size_t i = 1; i < node_->children.size(); ++i)
        labels.push_back(std::to_string(pos_.fullmoveNumber()) + (pos_.sideToMove() == White ? ". " : "… ") +
                         toSan(pos_, node_->children[i]->move));
    chooseFromList(app_, "Variations", labels, [this](int i) {
        if (i >= 0) enterVariation(i + 1);
    });
}

void GameStudyScreen::showInfo() {
    std::string text = row_.white + (row_.whiteElo ? " (" + std::to_string(row_.whiteElo) + ")" : "") + "\n" +
                       row_.black + (row_.blackElo ? " (" + std::to_string(row_.blackElo) + ")" : "") + "\n";
    if (!row_.event.empty()) text += row_.event + "\n";
    if (!row_.site.empty()) text += row_.site + "\n";
    if (!row_.date.empty()) text += row_.date + (row_.round.empty() ? "" : ", round " + row_.round) + "\n";
    if (!row_.eco.empty()) text += row_.eco + " " + row_.opening + "\n";
    text += std::to_string((row_.plies + 1) / 2) + " moves · " + row_.result;
    app_.showMessage("Game info", text);
}

void GameStudyScreen::analyzeHere() {
    // A fresh copy of the game (GameTree is not copyable), positioned at the same node.
    games::GamePack* pack = app_.gamePack();
    if (!pack) return;
    auto copy = pack->load(gameId_);
    if (!copy) return;
    std::vector<Move> path;
    for (const GameNode* n = node_; n && !n->isRoot(); n = n->parent) path.insert(path.begin(), n->move);
    app_.push(std::make_unique<AnalyzeScreen>(app_, std::move(copy->tree), path));
}

// --- Session (ADR 0011) --------------------------------------------------------------------------

bool GameStudyScreen::saveSession(std::map<std::string, std::string>& out) const {
    if (!loaded_) return false;
    std::string moves;
    std::vector<const GameNode*> path;
    for (const GameNode* n = node_; n && !n->isRoot(); n = n->parent) path.insert(path.begin(), n);
    for (const GameNode* n : path) moves += (moves.empty() ? "" : " ") + toUci(n->move);
    out["study.pack"] = pack_;
    out["study.game"] = std::to_string(gameId_);
    out["study.path"] = moves;
    out["summary"] = "Studying " + row_.white + " – " + row_.black + ".";
    return true;
}

std::unique_ptr<GameStudyScreen> GameStudyScreen::restore(Application& app, const std::map<std::string, std::string>& s) {
    auto get = [&](const char* k) {
        auto it = s.find(k);
        return it == s.end() ? std::string() : it->second;
    };
    games::GamePack* pack = app.gamePack();
    if (!pack || pack->name() != get("study.pack")) return nullptr;
    int64_t id = 0;
    const std::string idText = get("study.game");
    if (std::from_chars(idText.data(), idText.data() + idText.size(), id).ec != std::errc()) return nullptr;
    std::vector<std::string> path;
    const std::string moves = get("study.path");
    size_t i = 0;
    while (i < moves.size()) {
        size_t j = moves.find(' ', i);
        if (j == std::string::npos) j = moves.size();
        path.push_back(moves.substr(i, j - i));
        i = j + 1;
    }
    auto screen = std::make_unique<GameStudyScreen>(app, id, path);
    return screen->loaded() ? std::move(screen) : nullptr;
}

} // namespace pchess::app
