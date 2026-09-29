// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/SettingsScreen.h"

#include "app/Application.h"
#include "app/Chrome.h"
#include "app/Dialogs.h"
#include "core/engine/EngineDescriptor.h"
#include "core/util/Log.h"

#include <algorithm>

namespace pchess::app {

using namespace ui;

namespace {

enum NavId { NavPrev, NavNext };

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
    for (int id : {NavPrev, NavNext}) {
        Button b;
        b.id = id;
        b.label = id == NavPrev ? "‹ Prev page" : "Next page ›";
        b.rect = cells[id == NavPrev ? 0 : 2];
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
    for (const BoardTheme& t : BoardTheme::builtIns()) (t.mono ? monoThemes : colorThemes).push_back(t.name);
    add("Display", "Color theme", colorThemes, [&st, colorThemes] { return indexOf(colorThemes, st.boardTheme); },
        [&st, colorThemes](int i) { st.boardTheme = colorThemes[static_cast<size_t>(i)]; });
    add("Display", "Monochrome theme", monoThemes, [&st, monoThemes] { return indexOf(monoThemes, st.monoTheme); },
        [&st, monoThemes](int i) { st.monoTheme = monoThemes[static_cast<size_t>(i)]; });
    addBool("Display", "Coordinates", st.coordinates);
    addInts("Display", "Full refresh every", st.fullRefreshEvery, {6, 12, 20, 30, 50},
            {"6 updates", "12 updates", "20 updates", "30 updates", "50 updates"});

    // Input
    addBool("Input", "Tap to move", st.tapToMove);
    addBool("Input", "Drag to move", st.dragToMove);
    addBool("Input", "Show legal moves", st.showLegalMoves);

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
    focus_ = -1;
    app_.invalidateAll(RefreshHint::Content);
}

std::string SettingsScreen::valueText(const Item& it) const {
    const int i = it.current();
    return i >= 0 && i < static_cast<int>(it.choices.size()) ? it.choices[static_cast<size_t>(i)] : "—";
}

std::string SettingsScreen::focusedLabel() const {
    if (focus_ < 0) return {};
    const int item = pages_[static_cast<size_t>(page_)][static_cast<size_t>(focus_)].item;
    return item >= 0 ? items_[static_cast<size_t>(item)].label : std::string();
}

Rect SettingsScreen::focusRect() const {
    if (focus_ < 0) return {};
    return pages_[static_cast<size_t>(page_)][static_cast<size_t>(focus_)].rect;
}

void SettingsScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    c.fill(clip, t.background);
    const std::string pageText = std::to_string(page_ + 1) + "/" + std::to_string(pages_.size());
    drawStatusBar(app_, c, "Settings", pageText, "Back");

    Font& body = app_.fonts().get(s.fontPx);
    Font& bold = app_.fonts().get(s.fontPx, FontStyle::Bold);
    const Rgb dim = blend(t.background, t.ink, 150);
    const auto& rows = pages_[static_cast<size_t>(page_)];
    for (size_t r = 0; r < rows.size(); ++r) {
        const Row& row = rows[r];
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
        c.fill({row.rect.x, row.rect.bottom() - 1, row.rect.w, 1}, blend(t.background, t.frame, 90));
        if (static_cast<int>(r) == focus_) c.frame(row.rect, s.stroke, t.frame);
    }
    if (clip.intersects(footer_)) {
        for (const Button& b : nav_) drawButton(c, b, t, app_.fonts(), s);
        if (!app_.settingsPersisted()) {
            Font& small = app_.fonts().get(s.smallPx);
            const std::string note = "Not saved (no chess.db)";
            small.draw(c, footer_.x + (footer_.w - small.measure(note)) / 2, footer_.y + footer_.h / 2 + small.ascent() / 2,
                       note, dim);
        }
    }
}

void SettingsScreen::changed() {
    app_.saveSettings();
    app_.applySettings();  // redraws everything (theme, mono and coordinates may have changed)
}

void SettingsScreen::activate(int item) {
    Item& it = items_[static_cast<size_t>(item)];
    util::Log::info("settings", "open " + it.label);
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
    app_.push(std::make_unique<MessageDialog>(
        app_, it.label, "", buttons,
        [this, item](int i) {
            if (i < 0) return;
            Item& chosen = items_[static_cast<size_t>(item)];
            chosen.set(i);
            util::Log::info("settings", chosen.label + " = " + valueText(chosen));
            changed();
        },
        true));
}

void SettingsScreen::moveFocus(int delta) {
    const auto& rows = pages_[static_cast<size_t>(page_)];
    int f = focus_;
    for (size_t k = 0; k < rows.size(); ++k) {
        f += delta;
        if (f < 0 || f >= static_cast<int>(rows.size())) {
            // Past the page edge: go to the neighbouring page (focus on its first/last row).
            const int next = page_ + (delta > 0 ? 1 : -1);
            if (next < 0 || next >= static_cast<int>(pages_.size())) return;
            setPage(next);
            const auto& nrows = pages_[static_cast<size_t>(page_)];
            f = delta > 0 ? 0 : static_cast<int>(nrows.size()) - 1;
            while (f >= 0 && f < static_cast<int>(nrows.size()) && nrows[static_cast<size_t>(f)].item < 0) f += delta > 0 ? 1 : -1;
            focus_ = f;
            return;
        }
        if (rows[static_cast<size_t>(f)].item >= 0) break;
    }
    if (f < 0 || f >= static_cast<int>(rows.size()) || rows[static_cast<size_t>(f)].item < 0) return;
    const Rect old = focusRect();
    focus_ = f;
    if (!old.empty()) app_.invalidate(old, RefreshHint::Selection);
    app_.invalidate(focusRect(), RefreshHint::Selection);
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
    case KeyCode::Prev: case KeyCode::Up: moveFocus(-1); break;
    case KeyCode::Next: case KeyCode::Down: moveFocus(1); break;
    case KeyCode::Left: setPage(page_ - 1); break;
    case KeyCode::Right: setPage(page_ + 1); break;
    case KeyCode::Ok: case KeyCode::Menu:
        if (focus_ >= 0) activate(pages_[static_cast<size_t>(page_)][static_cast<size_t>(focus_)].item);
        break;
    case KeyCode::Back: case KeyCode::Home: app_.pop(); break;
    default: break;
    }
}

} // namespace pchess::app
