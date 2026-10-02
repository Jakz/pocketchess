// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/OpeningsScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/DrillScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/chess/San.h"
#include "core/games/Openings.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace pchess::app {

using namespace ui;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "1. e4 c5 2. Nf3 d6" for a line.
std::string movetext(const games::OpeningLine& l) {
    std::string out;
    chess::Position p = chess::Position::startPosition();
    for (chess::Move m : l.moves) {
        if (p.sideToMove() == chess::White) out += std::to_string(p.fullmoveNumber()) + ". ";
        out += chess::toSan(p, m) + " ";
        p.makeMove(m);
    }
    if (!out.empty()) out.pop_back();
    return out;
}

} // namespace

OpeningsScreen::OpeningsScreen(Application& app) : Screen(app) {
    layout();
    showFamilies();
}

void OpeningsScreen::layout() {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 3;
    // Controls at the bottom (one-hand use, ADR 0014): ◀ ▶ page, Find, Menu.
    const Rect navRect{pad, l.screen.h - pad - kMinTouchPx, l.screen.w - 2 * pad, kMinTouchPx};
    const auto cells = splitRow(navRect, 5, pad);  // [‹] then the four buttons
    backRect_ = cells[0];
    const char* labels[] = {"◀", "▶", "Find", "Menu"};
    for (int i = 0; i < 4; ++i) {
        Button b;
        b.id = i;
        b.label = labels[i];
        if (i < 2) b.hint = "page";
        b.rect = cells[static_cast<size_t>(i) + 1];
        nav_.push_back(b);
    }
    listRect_ = {pad, l.status.bottom() + pad, l.screen.w - 2 * pad, navRect.y - pad - l.status.bottom() - pad};
    rowH_ = std::max(kMinTouchPx, app_.fonts().get(s.fontPx).lineHeight() + app_.fonts().get(s.smallPx).lineHeight() +
                                      s.fontPx / 2);
    rowsPerPage_ = std::max(1, listRect_.h / rowH_);
}

int OpeningsScreen::pageCount() const {
    return std::max(1, (static_cast<int>(items_.size()) + rowsPerPage_ - 1) / rowsPerPage_);
}

void OpeningsScreen::showFamilies() {
    items_.clear();
    family_.clear();
    query_.clear();
    heading_ = "Openings";
    if (const games::OpeningBook* book = app_.openings()) {
        std::map<std::string, std::pair<int, std::string>> info;  // family -> lines, first ECO
        for (const auto& l : book->lines()) {
            auto& [n, eco] = info[l.family()];
            if (n++ == 0) eco = l.eco;
        }
        for (const std::string& f : book->families()) {
            const auto& [n, eco] = info[f];
            items_.push_back({f, eco + " · " + std::to_string(n) + (n == 1 ? " line" : " lines"), f, nullptr});
        }
    }
    page_ = 0;
    showPage();
}

void OpeningsScreen::showFamily(const std::string& family) {
    items_.clear();
    family_ = family;
    query_.clear();
    heading_ = family;
    if (const games::OpeningBook* book = app_.openings())
        for (const auto& l : book->lines())
            if (l.family() == family) {
                const size_t colon = l.name.find(':');
                const std::string variation = colon == std::string::npos ? "Main line" : l.name.substr(colon + 2);
                items_.push_back({variation, l.eco + " · " + movetext(l), {}, &l});
            }
    page_ = 0;
    showPage();
}

void OpeningsScreen::search(const std::string& text) {
    items_.clear();
    family_.clear();
    query_ = text;
    heading_ = "“" + text + "”";
    const std::string q = lower(text);
    if (const games::OpeningBook* book = app_.openings())
        for (const auto& l : book->lines())
            if (lower(l.name).find(q) != std::string::npos || lower(l.eco).rfind(q, 0) == 0)
                items_.push_back({l.name, l.eco + " · " + movetext(l), {}, &l});
    page_ = 0;
    showPage();
}

void OpeningsScreen::showPage() {
    // Games of the game pack that reached each visible line (its opening statistics), once per row.
    if (games::GamePack* pack = app_.gamePack()) {
        const size_t first = static_cast<size_t>(page_ * rowsPerPage_);
        for (size_t i = first; i < items_.size() && i < first + static_cast<size_t>(rowsPerPage_); ++i) {
            Item& it = items_[i];
            if (!it.line || it.counted) continue;
            it.counted = true;
            chess::Position p = chess::Position::startPosition();
            for (chess::Move m : it.line->moves) p.makeMove(m);
            int games = 0;
            if (auto moves = pack->bookMoves(p); moves)
                for (const auto& b : *moves) games += b.games;
            if (games > 0) it.detail += " · " + std::to_string(games) + (games == 1 ? " game" : " games");
        }
    }
    for (Button& b : nav_) {
        if (b.id == BtnPrev) b.enabled = page_ > 0;
        if (b.id == BtnNext) b.enabled = page_ + 1 < pageCount();
    }
    app_.invalidate(backRect_, RefreshHint::Content);  // its caption follows the level
    app_.invalidate(listRect_, RefreshHint::Page);
    app_.invalidate(app_.layout().status, RefreshHint::Content);
    for (const Button& b : nav_) app_.invalidate(b.rect, RefreshHint::Content);
}

void OpeningsScreen::turnPage(int delta) {
    const int p = std::clamp(page_ + delta, 0, pageCount() - 1);
    if (p == page_) return;
    page_ = p;
    showPage();
}

// --- Drawing -------------------------------------------------------------------------------------

void OpeningsScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    const std::string right = std::to_string(page_ + 1) + "/" + std::to_string(pageCount());
    drawStatusBar(app_, c, *this, heading_, right);
    drawBackButton(app_, c, *this, backRect_, family_.empty() && query_.empty() ? "" : "Openings");
    for (const Button& b : nav_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
    if (!clip.intersects(listRect_)) return;
    Font& body = app_.fonts().get(s.fontPx);
    Font& small = app_.fonts().get(s.smallPx);
    const Rgb dim = blend(t.background, t.ink, 160);
    if (items_.empty()) {
        const std::string text = app_.openings() ? "No opening matches." : "The opening book (assets/openings) is missing.";
        drawLines(c, listRect_.inset(s.fontPx / 2), small.wrap(text, listRect_.w - s.fontPx), small, dim);
        return;
    }
    c.pushClip(listRect_);
    const int pad = s.fontPx / 2;
    const size_t first = static_cast<size_t>(page_ * rowsPerPage_);
    for (size_t i = first; i < items_.size() && i < first + static_cast<size_t>(rowsPerPage_); ++i) {
        const Item& it = items_[i];
        const Rect row{listRect_.x, listRect_.y + static_cast<int>(i - first) * rowH_, listRect_.w, rowH_};
        const int y1 = row.y + pad / 2 + body.ascent();
        const std::string arrow = it.line ? "" : "›";
        body.draw(c, row.x + pad, y1, fitText(body, it.title, row.w - 3 * pad - body.measure(arrow)), t.ink);
        if (!arrow.empty()) body.draw(c, row.right() - pad - body.measure(arrow), y1, arrow, dim);
        small.draw(c, row.x + pad, y1 + body.descent() + small.ascent() + pad / 3, fitText(small, it.detail, row.w - 2 * pad),
                   dim);
        c.fill({row.x, row.bottom() - 1, row.w, 1}, blend(t.background, t.frame, 90));
    }
    c.popClip();
}

// --- Actions -------------------------------------------------------------------------------------

void OpeningsScreen::open(size_t index) {
    if (index >= items_.size()) return;
    const Item it = items_[index];
    if (!it.line) return showFamily(it.family);
    const games::OpeningLine* line = it.line;
    chooseFromList(app_, line->eco + " " + line->name, {"Study in Analyze", "Practice as White", "Practice as Black"},
                   [this, line](int i) {
                       if (i == 0) {
                           chess::GameTree tree;
                           chess::GameNode* n = tree.root();
                           for (chess::Move m : line->moves) n = tree.addMove(n, m);
                           app_.push(std::make_unique<AnalyzeScreen>(app_, std::move(tree), line->moves));
                       } else if (i > 0) {
                           app_.push(std::make_unique<DrillScreen>(app_, line->moves, i == 1 ? chess::White : chess::Black));
                       }
                   }, 6, {"the line, the engine and the book", "the book's moves after this line", ""});
}

void OpeningsScreen::find() {
    app_.requestText("Find an opening (name or ECO, e.g. Najdorf, B90)", query_, [this](std::optional<std::string> text) {
        if (!text) return;
        if (text->empty()) return showFamilies();
        search(*text);
    });
}

void OpeningsScreen::openMenu() {
    enum Action { Choose, Find, Practice, All, Back };
    std::vector<std::string> labels;
    std::vector<Action> actions;
    if (!items_.empty()) {  // key-only use: the reader's buttons are Prev, Next and Menu
        labels.push_back(family_.empty() && query_.empty() ? "Open a family…" : "Open a line…");
        actions.push_back(Choose);
    }
    labels.push_back("Find an opening…");
    actions.push_back(Find);
    labels.push_back("Practice from the start…");
    actions.push_back(Practice);
    if (!family_.empty() || !query_.empty()) {
        labels.push_back("All openings");
        actions.push_back(All);
    }
    labels.push_back(backMenuLabel(app_, *this));
    actions.push_back(Back);
    labels.push_back("Close");
    app_.push(std::make_unique<MessageDialog>(
        app_, heading_, "", labels,
        [this, actions](int i) {
            if (i < 0 || i >= static_cast<int>(actions.size())) return;
            switch (actions[static_cast<size_t>(i)]) {
            case Choose: {
                std::vector<std::string> names;
                for (const Item& it : items_) names.push_back(it.title);
                chooseFromList(app_, heading_, names, [this](int k) {
                    if (k >= 0) open(static_cast<size_t>(k));
                }, rowsPerPage_);
                break;
            }
            case Find: find(); break;
            case Practice: DrillScreen::choose(app_, {}, "Practice from the start"); break;
            case All: showFamilies(); break;
            case Back: app_.pop(); break;
            }
        },
        true));
}

// --- Input ---------------------------------------------------------------------------------------

void OpeningsScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    if (backRect_.contains(g.start)) {
        // Inside a family or a search, Back first returns to the families.
        if (!family_.empty() || !query_.empty()) return showFamilies();
        return app_.pop();
    }
    if (listRect_.contains(g.start) && rowH_ > 0) {
        const size_t row = static_cast<size_t>((g.start.y - listRect_.y) / rowH_);
        if (row < static_cast<size_t>(rowsPerPage_)) open(static_cast<size_t>(page_ * rowsPerPage_) + row);
        return;
    }
    switch (hitTest(nav_, g.start)) {
    case BtnPrev: turnPage(-1); break;
    case BtnNext: turnPage(1); break;
    case BtnFind: find(); break;
    case BtnMenu: openMenu(); break;
    default: break;
    }
}

void OpeningsScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Left: case KeyCode::Up: turnPage(-1); break;
    case KeyCode::Next: case KeyCode::Right: case KeyCode::Down: turnPage(1); break;
    case KeyCode::Ok: case KeyCode::Menu: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

} // namespace pchess::app
