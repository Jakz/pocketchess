// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/SettingsScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/engine/EngineDescriptor.h"
#include "core/util/Log.h"
#include "ui/theme/Palette.h"

#include <algorithm>

namespace pchess::app {

using namespace ui;

namespace {

enum NavId { NavPrev, NavNext, NavMenu };

// Index of `value` in `values`, or -1.
template <class T>
int indexOf(const std::vector<T>& values, const T& value) {
    auto it = std::find(values.begin(), values.end(), value);
    return it == values.end() ? -1 : static_cast<int>(it - values.begin());
}

std::string capitalized(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

} // namespace

SettingsScreen::SettingsScreen(Application& app) : Screen(app) {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int pad = s.fontPx / 2;
    footer_ = {pad, l.screen.h - kMinTouchPx - 2 * pad, l.screen.w - 2 * pad, kMinTouchPx + pad};
    body_ = {pad, l.status.bottom() + pad, l.screen.w - 2 * pad, footer_.y - l.status.bottom() - 2 * pad};
    const auto cells = splitRow({footer_.x, footer_.y + pad, footer_.w, kMinTouchPx}, 3, pad);
    const char* labels[] = {"◀", "▶", "Menu"};  // ◀ ▶ = the Prev/Next keys
    for (int id : {NavPrev, NavNext, NavMenu}) {
        Button b;
        b.id = id;
        b.label = labels[id];
        if (id != NavMenu) b.hint = "page";
        b.rect = cells[static_cast<size_t>(id)];
        nav_.push_back(b);
    }
    buildItems();
    paginate();
    setPage(0);
}

void SettingsScreen::buildItems() {
    Settings& st = app_.settings();
    auto onOff = [](bool& flag) {
        return std::pair<std::function<int()>, std::function<void(int)>>{[&flag] { return flag ? 0 : 1; },
                                                                         [&flag](int i) { flag = i == 0; }};
    };
    auto add = [this](std::string section, std::string label, std::vector<std::string> choices,
                      std::function<int()> current, std::function<void(int)> set) {
        items_.push_back({std::move(section), std::move(label), std::move(choices), std::move(current), std::move(set)});
    };
    auto addBool = [&](std::string section, std::string label, bool& flag, std::string on = "On", std::string off = "Off") {
        auto [cur, set] = onOff(flag);
        add(std::move(section), std::move(label), {std::move(on), std::move(off)}, cur, set);
    };
    auto addInts = [&](std::string section, std::string label, int& value, std::vector<int> values,
                       std::vector<std::string> texts) {
        add(std::move(section), std::move(label), std::move(texts), [&value, values] { return indexOf(values, value); },
            [&value, values](int i) { value = values[static_cast<size_t>(i)]; });
    };

    // Display
    add("Display", "Mode", {"Color", "Monochrome"}, [&st] { return st.mono ? 1 : 0; },
        [&st](int i) { st.mono = i == 1; });
    std::vector<std::string> colorThemes, monoThemes;
    for (const BoardTheme& t : app_.themes().themes()) (t.mono ? monoThemes : colorThemes).push_back(t.name);
    add("Display", "Color theme", colorThemes, [&st, colorThemes] { return indexOf(colorThemes, st.boardTheme); },
        [&st, colorThemes](int i) { st.boardTheme = colorThemes[static_cast<size_t>(i)]; });
    items_.back().swatches = [this, colorThemes] {
        std::vector<Swatch> out;
        for (const std::string& n : colorThemes)
            if (const BoardTheme* t = app_.themes().find(n)) out.push_back({n, t->light, t->dark});
        return out;
    };
    // Accent and piece colors (color mode): the hues of ui::accentColors().
    std::vector<std::string> hues{""}, accentTexts{"Theme"}, pieceTexts{"Black & white"};
    for (const NamedColor& c : accentColors()) {
        hues.push_back(c.name);
        accentTexts.push_back(c.name);
        pieceTexts.push_back(c.name);
    }
    add("Display", "Accent color", accentTexts, [&st, hues] { return indexOf(hues, st.accent); },
        [&st, hues](int i) { st.accent = hues[static_cast<size_t>(i)]; });
    items_.back().swatches = [this] {
        const BoardTheme own = app_.themes().resolve(app_.settings().boardTheme, false);
        std::vector<Swatch> out{{"Theme", own.accent, std::nullopt}};
        for (const NamedColor& c : accentColors()) out.push_back({c.name, c.color, std::nullopt});
        return out;
    };
    add("Display", "Piece colors", pieceTexts, [&st, hues] { return indexOf(hues, st.pieceColor); },
        [&st, hues](int i) { st.pieceColor = hues[static_cast<size_t>(i)]; });
    items_.back().swatches = [] {
        std::vector<Swatch> out{{"Black & white", Rgb::gray(255), Rgb::gray(0)}};
        for (const NamedColor& c : accentColors()) {
            BoardTheme t;
            applyPieceTint(t, c.color);
            out.push_back({c.name, t.whitePiece, t.blackPiece});
        }
        return out;
    };
    add("Display", "Monochrome theme", monoThemes, [&st, monoThemes] { return indexOf(monoThemes, st.monoTheme); },
        [&st, monoThemes](int i) { st.monoTheme = monoThemes[static_cast<size_t>(i)]; });
    std::vector<std::string> pieceSets;
    for (const PieceSetInfo& s : app_.pieceSets().sets()) pieceSets.push_back(s.name);
    pieceSets.push_back("DejaVu");  // the built-in font glyphs
    add("Display", "Piece set", pieceSets,
        [&st, pieceSets] {  // a set that is not installed falls back to the glyphs (Application::applyPieceSet)
            const int i = indexOf(pieceSets, st.pieceSet);
            return i >= 0 ? i : static_cast<int>(pieceSets.size()) - 1;
        },
        [&st, pieceSets](int i) { st.pieceSet = pieceSets[static_cast<size_t>(i)]; });
    addBool("Display", "Coordinates", st.coordinates);
    addInts("Display", "Full refresh every", st.fullRefreshEvery, {6, 12, 20, 30, 50},
            {"6 updates", "12 updates", "20 updates", "30 updates", "50 updates"});

    // Input
    addBool("Input", "Tap to move", st.tapToMove);
    addBool("Input", "Drag to move", st.dragToMove);
    addBool("Input", "Show legal moves", st.showLegalMoves);
    addBool("Input", "Page buttons", st.swapPageKeys, "Swapped", "Normal");

    // Engine / Power
    add("Engine", "Power profile", {"Performance", "Balanced", "Eco"},
        [&st] { return static_cast<int>(st.powerProfile); },
        [&st](int i) { st.powerProfile = static_cast<engine::PowerProfileId>(i); });
    std::vector<std::string> ids{""}, names{"Profile default"};
    for (const auto& e : app_.engineRegistry().entries())
        if (e.available) {
            ids.push_back(e.descriptor.id);
            names.push_back(e.descriptor.name);
        }
    add("Engine", "Analysis engine", names, [&st, ids] { return indexOf(ids, st.analysisEngine); },
        [&st, ids](int i) { st.analysisEngine = ids[static_cast<size_t>(i)]; });
    add("Engine", "Play engine", names, [&st, ids] { return indexOf(ids, st.playEngine); },
        [&st, ids](int i) { st.playEngine = ids[static_cast<size_t>(i)]; });
    std::vector<std::string> labels = engine::strengthLabels(), labelTexts;
    for (const auto& s : labels) labelTexts.push_back(capitalized(s));
    add("Engine", "Default strength", labelTexts, [&st, labels] { return indexOf(labels, st.playStrength); },
        [&st, labels](int i) { st.playStrength = labels[static_cast<size_t>(i)]; });
    addInts("Engine", "Deep analysis stops", st.deepLimitSeconds, {0, 10, 30, 60, 120, -1},
            {"Profile default", "After 10 s", "After 30 s", "After 60 s", "After 2 min", "When I stop it"});
    addInts("Engine", "Analysis lines", st.analysisMultiPv, {1, 2, 3}, {"1", "2", "3"});
    addBool("Engine", "Evaluation", st.evalFromWhite, "White's view", "Side to move");
    addInts("Engine", "Threads", st.threads, {0, 1, 2, 4}, {"Profile default", "1", "2", "4"});
    addInts("Engine", "Hash", st.hashMb, {0, 16, 32, 64}, {"Profile default", "16 MB", "32 MB", "64 MB"});

    // Tactics (design notes §20)
    std::vector<std::string> packs{""}, packTexts{"Largest installed"};
    for (const auto& n : app_.puzzlePackNames()) {
        packs.push_back(n);
        packTexts.push_back(n);
    }
    add("Tactics", "Puzzle pack", packTexts, [&st, packs] { return indexOf(packs, st.tacticsPack); },
        [&st, packs](int i) { st.tacticsPack = packs[static_cast<size_t>(i)]; });
    std::vector<std::string> themes{""}, themeTexts{"Any"};
    if (puzzles::PuzzlePack* pack = app_.puzzlePack()) {
        std::vector<std::string> themeList;
        for (const auto& t : pack->themeNames())
            if (!t.empty()) themeList.push_back(t);
        std::sort(themeList.begin(), themeList.end());
        for (const auto& t : themeList) {
            themes.push_back(t);
            themeTexts.push_back(t);
        }
    }
    add("Tactics", "Theme", themeTexts, [&st, themes] { return indexOf(themes, st.tacticsTheme); },
        [&st, themes](int i) { st.tacticsTheme = themes[static_cast<size_t>(i)]; });
    std::vector<std::string> skipTexts = themeTexts;
    skipTexts[0] = "None";
    add("Tactics", "Skip theme", skipTexts, [&st, themes] { return indexOf(themes, st.tacticsSkipTheme); },
        [&st, themes](int i) { st.tacticsSkipTheme = themes[static_cast<size_t>(i)]; });
    addInts("Tactics", "Difficulty", st.tacticsDifficulty, {-200, 0, 200}, {"Easier", "Normal", "Harder"});
    addBool("Tactics", "Opponent's move", st.tacticsAutoOpponent, "Automatic", "Tap Next");
    addBool("Tactics", "Next puzzle after a solve", st.tacticsAutoNext, "Automatic", "Tap Next");

    // Advanced
    addInts("Advanced", "Review clock offset", st.clockOffsetDays, {0, 1, 3, 7, 30},
            {"None", "+1 day", "+3 days", "+7 days", "+30 days"});
    addBool("Advanced", "Debug logging", st.debugLogging);
}

void SettingsScreen::paginate() {
    const UiStyle& s = app_.style();
    const int headerH = app_.fonts().get(s.fontPx, FontStyle::Bold).lineHeight() + s.fontPx / 2;
    const int rowH = kMinTouchPx;
    pages_.clear();
    std::vector<Row> page;
    int y = body_.y;
    std::string section;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const Item& it = items_[static_cast<size_t>(i)];
        const bool needHeader = it.section != section || page.empty();
        const int need = (needHeader ? headerH : 0) + rowH;
        if (y + need > body_.bottom() && !page.empty()) {
            pages_.push_back(std::move(page));
            page.clear();
            y = body_.y;
        }
        if (it.section != section || page.empty()) {
            page.push_back({-1, it.section, {body_.x, y, body_.w, headerH}});
            y += headerH;
            section = it.section;
        }
        page.push_back({i, {}, {body_.x, y, body_.w, rowH}});
        y += rowH;
    }
    if (!page.empty()) pages_.push_back(std::move(page));
}

void SettingsScreen::setPage(int page) {
    page_ = std::clamp(page, 0, static_cast<int>(pages_.size()) - 1);
    nav_[NavPrev].enabled = page_ > 0;
    nav_[NavNext].enabled = page_ + 1 < static_cast<int>(pages_.size());
    app_.invalidateAll(RefreshHint::Content);
}

std::string SettingsScreen::valueText(const Item& it) const {
    const int i = it.current();
    return i >= 0 && i < static_cast<int>(it.choices.size()) ? it.choices[static_cast<size_t>(i)] : "—";
}

bool SettingsScreen::open(const std::string& label) {
    for (size_t p = 0; p < pages_.size(); ++p)
        for (const Row& row : pages_[p])
            if (row.item >= 0 && items_[static_cast<size_t>(row.item)].label == label) {
                if (static_cast<int>(p) != page_) setPage(static_cast<int>(p));
                activate(row.item);
                return true;
            }
    return false;
}

void SettingsScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    const std::string pageText = std::to_string(page_ + 1) + "/" + std::to_string(pages_.size()) +
                                 (app_.settingsPersisted() ? "" : " · not saved");
    drawStatusBar(app_, c, *this, "Settings", pageText);

    Font& body = app_.fonts().get(s.fontPx);
    Font& bold = app_.fonts().get(s.fontPx, FontStyle::Bold);
    const Rgb dim = blend(t.background, t.ink, 150);
    const auto& rows = pages_[static_cast<size_t>(page_)];
    for (const Row& row : rows) {
        if (!row.rect.intersects(clip)) continue;
        if (row.item < 0) {
            bold.draw(c, row.rect.x, row.rect.bottom() - s.fontPx / 3, row.header, t.ink);
            c.fill({row.rect.x, row.rect.bottom() - 2, row.rect.w, 2}, t.frame);
            continue;
        }
        const Item& it = items_[static_cast<size_t>(row.item)];
        const int baseline = row.rect.y + (row.rect.h + body.ascent()) / 2 - s.stroke;
        const int pad = s.fontPx / 2;
        body.draw(c, row.rect.x + pad, baseline, it.label, t.ink);
        const std::string value = valueText(it);
        body.draw(c, row.rect.right() - pad - body.measure(value), baseline, value, dim);
        if (it.swatches) {  // the current color next to its name
            const auto sw = it.swatches();
            const int cur = it.current();
            if (cur >= 0 && cur < static_cast<int>(sw.size())) {
                const int sz = body.lineHeight();
                const Rect box{row.rect.right() - pad - body.measure(value) - pad - 2 * sz, row.rect.y + (row.rect.h - sz) / 2,
                               2 * sz, sz};
                const Swatch& s0 = sw[static_cast<size_t>(cur)];
                c.fill(s0.b ? Rect{box.x, box.y, box.w / 2, box.h} : box, s0.a);
                if (s0.b) c.fill({box.x + box.w / 2, box.y, box.w - box.w / 2, box.h}, *s0.b);
                c.frame(box, s.stroke, t.frame);
            }
        }
        c.fill({row.rect.x, row.rect.bottom() - 1, row.rect.w, 1}, blend(t.background, t.frame, 90));
    }
    for (const Button& b : nav_)
        if (clip.intersects(b.rect)) drawButton(c, b, t, app_.fonts(), s);
}

void SettingsScreen::changed() {
    app_.saveSettings();
    app_.applySettings();  // redraws everything (theme, mono and coordinates may have changed)
}
void SettingsScreen::activate(int item) {
    Item& it = items_[static_cast<size_t>(item)];
    util::Log::info("settings", "open " + it.label);
    if (it.swatches) {  // color rows: pick from colored rectangles
        app_.push(std::make_unique<SwatchDialog>(app_, it.label, it.swatches(), it.current(), [this, item](int i) {
            if (i < 0) return;
            Item& chosen = items_[static_cast<size_t>(item)];
            chosen.set(i);
            util::Log::info("settings", chosen.label + " = " + valueText(chosen));
            changed();
        }));
        return;
    }
    if (it.choices.size() == 2) {  // toggles in place
        const int cur = it.current();
        it.set(cur == 0 ? 1 : 0);
        util::Log::info("settings", it.label + " = " + valueText(it));
        changed();
        return;
    }
    std::vector<std::string> buttons;
    const int cur = it.current();
    for (size_t i = 0; i < it.choices.size(); ++i)
        buttons.push_back((static_cast<int>(i) == cur ? "✓ " : "") + it.choices[i]);
    auto done = [this, item](int i) {
        if (i < 0) return;
        Item& chosen = items_[static_cast<size_t>(item)];
        chosen.set(i);
        util::Log::info("settings", chosen.label + " = " + valueText(chosen));
        changed();
    };
    if (buttons.size() > 7) return chooseFromList(app_, it.label, buttons, done);  // long lists page
    app_.push(std::make_unique<MessageDialog>(app_, it.label, "", buttons, done, true));
}

// Key-only use (the reader's buttons are Prev, Next and Menu): Prev/Next turn pages, Menu offers
// the settings of the page, Back and Close.
void SettingsScreen::openMenu() {
    std::vector<int> ids;
    std::vector<std::string> labels;
    for (const Row& row : pages_[static_cast<size_t>(page_)])
        if (row.item >= 0) {
            ids.push_back(row.item);
            labels.push_back(items_[static_cast<size_t>(row.item)].label + ": " +
                             valueText(items_[static_cast<size_t>(row.item)]));
        }
    app_.push(std::make_unique<MessageDialog>(
        app_, "Settings", "", std::vector<std::string>{"Change a setting…", backMenuLabel(app_, *this), "Close"},
        [this, ids, labels](int i) {
            if (i == 1) return app_.pop();
            if (i != 0) return;
            chooseFromList(app_, "Settings " + std::to_string(page_ + 1) + "/" + std::to_string(pages_.size()), labels,
                           [this, ids](int k) {
                               if (k >= 0) activate(ids[static_cast<size_t>(k)]);
                           });
        },
        true));
}

void SettingsScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    if (statusBackRect(app_).contains(g.start)) {
        app_.pop();
        return;
    }
    switch (hitTest(nav_, g.start)) {
    case NavPrev: setPage(page_ - 1); return;
    case NavNext: setPage(page_ + 1); return;
    case NavMenu: openMenu(); return;
    default: break;
    }
    for (const Row& row : pages_[static_cast<size_t>(page_)])
        if (row.item >= 0 && row.rect.contains(g.start)) {
            activate(row.item);
            return;
        }
}

void SettingsScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Up: case KeyCode::Left: setPage(page_ - 1); break;
    case KeyCode::Next: case KeyCode::Down: case KeyCode::Right: setPage(page_ + 1); break;
    case KeyCode::Ok: case KeyCode::Menu: openMenu(); break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

} // namespace pchess::app
