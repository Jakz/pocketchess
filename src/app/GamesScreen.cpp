// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/GamesScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "app/GameStudyScreen.h"
#include "core/storage/ChessDb.h"
#include "core/util/Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace pchess::app {

using namespace ui;

namespace {

struct YearRange {
    const char* label;
    int from, to;
};
constexpr YearRange kYears[] = {{"Any year", 0, 0},        {"Before 1900", 0, 1899}, {"1900–1949", 1900, 1949},
                                {"1950–1999", 1950, 1999}, {"2000–2009", 2000, 2009}, {"2010–2019", 2010, 2019},
                                {"2020 or later", 2020, 0}};

std::string resultText(const std::string& r) { return r == "1/2-1/2" ? "½-½" : r; }

bool looksLikeEco(const std::string& s) {
    if (s.empty() || s.size() > 3 || s[0] < 'A' || s[0] > 'E') return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

} // namespace

GamesScreen::GamesScreen(Application& app) : Screen(app) {
    layout();
    query();
}

void GamesScreen::layout() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    const int btnH = kMinTouchPx;
    // Controls at the bottom (one-hand use): filter chips right above the ◀ ▶ Menu bar.
    const Rect navRect{pad, l.screen.h - pad - btnH, l.screen.w - 2 * pad, btnH};
    const Rect chipsRect{pad, navRect.y - pad - btnH, l.screen.w - 2 * pad, btnH};
    const char* chipLabels[] = {"Player", "Year", "Result", "Opening", "☆"};
    const auto chipCells = splitRow(chipsRect, 5, pad);
    for (int i = 0; i < 5; ++i) {
        Button b;
        b.id = i;
        b.label = chipLabels[i];
        b.rect = chipCells[static_cast<size_t>(i)];
        chips_.push_back(b);
    }
    const auto navCells = splitRow(navRect, 4, pad);  // [‹] ◀ ▶ Menu
    backRect_ = navCells[0];
    const char* navLabels[] = {"◀", "▶", "Menu"};  // ◀ ▶ = the Prev/Next keys
    for (int i = 0; i < 3; ++i) {
        Button b;
        b.id = BtnPrev + i;
        b.label = navLabels[i];
        if (i < 2) b.hint = "page";
        b.rect = navCells[static_cast<size_t>(i) + 1];
        nav_.push_back(b);
    }
    listRect_ = {pad, l.status.bottom() + pad, l.screen.w - 2 * pad, chipsRect.y - pad - l.status.bottom() - pad};
    // Two text lines per row, never below the minimum touch height.
    rowH_ = std::max(kMinTouchPx, app_.fonts().get(s.fontPx).lineHeight() + app_.fonts().get(s.smallPx).lineHeight() +
                                      s.fontPx / 2);
    rowsPerPage_ = std::max(1, listRect_.h / rowH_);
}

int GamesScreen::pageCount() const { return std::max(1, (total_ + rowsPerPage_ - 1) / rowsPerPage_); }

// Filtered totals stop at GamePack::kCountCap: beyond it, turn pages while they come back full.
bool GamesScreen::capped() const { return total_ > games::GamePack::kCountCap; }
bool GamesScreen::hasNextPage() const {
    return capped() ? static_cast<int>(rows_.size()) == rowsPerPage_ : page_ + 1 < pageCount();
}

void GamesScreen::setFilter(games::GameFilter f, bool favoritesOnly) {
    filter_ = std::move(f);
    favoritesOnly_ = favoritesOnly;
    query();
}

void GamesScreen::query() {
    rows_.clear();
    total_ = 0;
    page_ = 0;
    message_.clear();
    games::GamePack* pack = app_.gamePack();
    title_ = "Games";
    if (!pack) {
        message_ = "No game pack installed. Copy a .pgn or a game pack (.db) into imports/ and open Home → Import.";
    } else {
        const auto meta = pack->meta();
        if (auto it = meta.find("title"); it != meta.end() && !it->second.empty()) title_ = it->second;
        games::GameFilter f = filter_;
        if (favoritesOnly_) {
            f.ids = std::vector<int64_t>{};
            if (storage::Database* db = app_.userDb())
                if (auto ids = storage::favoriteIds(*db, pack->name()); ids) f.ids = std::move(*ids);
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto n = pack->count(f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        util::Log::info("games", "count " + std::to_string(n ? *n : -1) + " in " + std::to_string(int(ms + 0.5)) +
                                     " ms (pack " + pack->name() + ", " + std::to_string(pack->count()) + " games)");
        if (!n) message_ = n.error().message();
        else total_ = *n;
        if (n && total_ == 0) message_ = favoritesOnly_ ? "No favorites yet: ☆ a game in its study view." : "No games match.";
    }
    updateChips();
    loadPage();
}

void GamesScreen::loadPage() {
    rows_.clear();
    games::GamePack* pack = app_.gamePack();
    if (pack && total_ > 0) {
        games::GameFilter f = filter_;
        if (favoritesOnly_) {
            f.ids = std::vector<int64_t>{};
            if (storage::Database* db = app_.userDb())
                if (auto ids = storage::favoriteIds(*db, pack->name()); ids) f.ids = std::move(*ids);
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto rows = pack->page(f, page_ * rowsPerPage_, rowsPerPage_);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        util::Log::info("games", "page " + std::to_string(page_ + 1) + "/" + std::to_string(pageCount()) + " in " +
                                     std::to_string(int(ms + 0.5)) + " ms");
        if (rows) rows_ = std::move(*rows);
        else message_ = rows.error().message();
    }
    for (Button& b : nav_) {
        if (b.id == BtnPrev) b.enabled = page_ > 0;
        if (b.id == BtnNext) b.enabled = hasNextPage();
    }
    // A page turn repaints the list and the page counter only (partial refresh, M5 acceptance).
    app_.invalidate(listRect_, RefreshHint::Page);
    app_.invalidate(app_.layout().status, RefreshHint::Content);
    for (const Button& b : nav_) app_.invalidate(b.rect, RefreshHint::Content);
}

void GamesScreen::nextPage() {
    if (!hasNextPage()) return;
    ++page_;
    loadPage();
}

void GamesScreen::prevPage() {
    if (page_ == 0) return;
    --page_;
    loadPage();
}

void GamesScreen::updateChips() {
    for (Button& b : chips_) {
        switch (b.id) {
        case BtnPlayer:
            b.hint = filter_.player.empty() ? "any" : filter_.player;
            if (!filter_.opponent.empty()) b.hint += " – " + filter_.opponent;
            break;
        case BtnYear: {
            b.hint = "any";
            for (const YearRange& y : kYears)
                if (y.from == filter_.yearFrom && y.to == filter_.yearTo && (y.from || y.to)) b.hint = y.label;
            break;
        }
        case BtnResult:
            b.hint = filter_.result.empty() ? "any" : resultText(filter_.result);
            break;
        case BtnOpening: b.hint = !filter_.eco.empty() ? filter_.eco : filter_.opening.empty() ? "any" : filter_.opening; break;
        case BtnFavorites:
            b.label = favoritesOnly_ ? "★" : "☆";
            b.hint = favoritesOnly_ ? "favorites" : "all";
            break;
        default: break;
        }
        b.primary = (b.id == BtnPlayer && !filter_.player.empty()) || (b.id == BtnYear && (filter_.yearFrom || filter_.yearTo)) ||
                    (b.id == BtnResult && !filter_.result.empty()) ||
                    (b.id == BtnOpening && (!filter_.eco.empty() || !filter_.opening.empty())) ||
                    (b.id == BtnFavorites && favoritesOnly_);
        app_.invalidate(b.rect, RefreshHint::Content);
    }
}

// --- Drawing -------------------------------------------------------------------------------------

void GamesScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    const std::string right = total_ <= 0 ? ""
                              : std::to_string(page_ + 1) + "/" + std::to_string(pageCount() - (capped() ? 1 : 0)) +
                                    (capped() ? "+" : "");
    drawStatusBar(app_, c, *this, title_, right);
    for (const Button& b : chips_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
    drawBackButton(app_, c, *this, backRect_);
    for (const Button& b : nav_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
    if (!clip.intersects(listRect_)) return;
    Font& body = app_.fonts().get(s.fontPx);
    Font& small = app_.fonts().get(s.smallPx);
    const Rgb dim = blend(t.background, t.ink, 160);
    if (rows_.empty()) {
        drawLines(c, listRect_.inset(s.fontPx / 2), small.wrap(message_, listRect_.w - s.fontPx), small, dim);
        return;
    }
    c.pushClip(listRect_);
    const int pad = s.fontPx / 2;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const games::GameRow& r = rows_[i];
        const Rect row{listRect_.x, listRect_.y + static_cast<int>(i) * rowH_, listRect_.w, rowH_};
        const std::string result = resultText(r.result);
        const int resultW = body.measure(result);
        const std::string players = fitText(body, r.white + " – " + r.black, row.w - resultW - 3 * pad);
        const int y1 = row.y + pad / 2 + body.ascent();
        body.draw(c, row.x + pad, y1, players, t.ink);
        body.draw(c, row.right() - pad - resultW, y1, result, t.ink);
        std::string line2 = r.date.size() >= 4 && r.date[0] != '?' ? r.date.substr(0, 4) : "";
        for (const std::string& part : {r.event, r.eco.empty() ? r.opening : r.eco + " " + r.opening})
            if (!part.empty()) line2 += (line2.empty() ? "" : " · ") + part;
        small.draw(c, row.x + pad, y1 + body.descent() + small.ascent() + pad / 3,
                   fitText(small, line2, row.w - 2 * pad), dim);
        c.fill({row.x, row.bottom() - 1, row.w, 1}, blend(t.background, t.frame, 90));
    }
    c.popClip();
}

// --- Input ---------------------------------------------------------------------------------------

void GamesScreen::open(size_t row) {
    if (row >= rows_.size()) return;
    app_.push(std::make_unique<GameStudyScreen>(app_, rows_[row].id));
}

void GamesScreen::onEnter() {
    if (favoritesOnly_) query();  // a favorite may have been removed in the study view
}

void GamesScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    if (backRect_.contains(g.start)) return app_.pop();
    if (listRect_.contains(g.start) && rowH_ > 0) return open(static_cast<size_t>((g.start.y - listRect_.y) / rowH_));
    switch (hitTest(chips_, g.start)) {
    case BtnPlayer: return editPlayer();
    case BtnYear: return chooseYear();
    case BtnResult: return chooseResult();
    case BtnOpening: return editOpening();
    case BtnFavorites:
        favoritesOnly_ = !favoritesOnly_;
        return query();
    default: break;
    }
    switch (hitTest(nav_, g.start)) {
    case BtnPrev: prevPage(); break;
    case BtnNext: nextPage(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void GamesScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: case KeyCode::Up: prevPage(); break;
    case KeyCode::Next: case KeyCode::Right: case KeyCode::Down: nextPage(); break;
    case KeyCode::Menu: case KeyCode::Ok: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

void GamesScreen::editPlayer() {
    // "Name" or "Name - Opponent".
    std::string current = filter_.player + (filter_.opponent.empty() ? "" : " - " + filter_.opponent);
    app_.requestText("Player (or Player - Opponent)", current, [this](std::optional<std::string> text) {
        if (!text) return;
        std::string s = *text;
        filter_.player.clear();
        filter_.opponent.clear();
        const size_t dash = s.find(" - ");
        filter_.player = s.substr(0, dash);
        if (dash != std::string::npos) filter_.opponent = s.substr(dash + 3);
        query();
    });
}

void GamesScreen::chooseYear() {
    std::vector<std::string> labels;
    for (const YearRange& y : kYears) labels.push_back(y.label);
    chooseFromList(app_, "Year", labels, [this](int i) {
        if (i < 0) return;
        filter_.yearFrom = kYears[i].from;
        filter_.yearTo = kYears[i].to;
        query();
    }, 7);
}

void GamesScreen::chooseResult() {
    static const char* values[] = {"", "1-0", "0-1", "1/2-1/2"};
    chooseFromList(app_, "Result", {"Any result", "White won (1-0)", "Black won (0-1)", "Draw (½-½)"}, [this](int i) {
        if (i < 0) return;
        filter_.result = values[i];
        query();
    });
}

void GamesScreen::editOpening() {
    std::string current = !filter_.eco.empty() ? filter_.eco : filter_.opening;
    app_.requestText("Opening name or ECO (e.g. Sicilian, B90)", current, [this](std::optional<std::string> text) {
        if (!text) return;
        std::string s = *text;
        for (char& ch : s)
            if (s.size() <= 3) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        filter_.eco.clear();
        filter_.opening.clear();
        if (looksLikeEco(s)) filter_.eco = s;
        else filter_.opening = *text;
        query();
    });
}

void GamesScreen::openMenu() {
    enum Action { Open, Pack, Clear, Home };
    std::vector<std::string> labels;
    std::vector<Action> actions;
    if (!rows_.empty()) {  // key-only use: the reader's buttons are Prev, Next and Menu
        labels.push_back("Open a game…");
        actions.push_back(Open);
    }
    const auto packs = app_.gamePackNames();
    if (packs.size() > 1) {
        labels.push_back("Pack…");
        actions.push_back(Pack);
    }
    labels.push_back("Clear filters");
    actions.push_back(Clear);
    labels.push_back(backMenuLabel(app_, *this));
    actions.push_back(Home);
    labels.push_back("Close");
    app_.push(std::make_unique<MessageDialog>(
        app_, "Games", "", labels,
        [this, actions, packs](int i) {
            if (i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Open: {
                std::vector<std::string> games;
                for (const games::GameRow& r : rows_) games.push_back(r.white + " – " + r.black);
                chooseFromList(app_, "Open", games, [this](int g) {
                    if (g >= 0) open(static_cast<size_t>(g));
                }, rowsPerPage_);
                break;
            }
            case Pack:
                chooseFromList(app_, "Game pack", packs, [this, packs](int p) {
                    if (p < 0) return;
                    app_.settings().gamesPack = packs[static_cast<size_t>(p)];
                    app_.saveSettings();
                    query();
                    app_.invalidateAll(RefreshHint::Content);
                });
                break;
            case Clear:
                filter_ = {};
                favoritesOnly_ = false;
                query();
                break;
            case Home: app_.pop(); break;
            }
        },
        true));
}

} // namespace pchess::app
