// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/HomeScreen.h"

#include "app/AnalyzeScreen.h"
#include "app/Application.h"
#include "app/Chrome.h"
#include "app/DiagnosticsScreen.h"
#include "app/Dialogs.h"
#include "app/PlayScreen.h"
#include "core/engine/EngineDescriptor.h"
#include "app/SettingsScreen.h"

namespace pchess::app {

using namespace ui;

namespace {
enum Id { Play, Tactics, Games, Analyze, Review, SettingsItem, Diagnostics, Exit };
}

HomeScreen::HomeScreen(Application& app) : Screen(app) {
    const Layout& l = app_.layout();
    const UiStyle& s = app_.style();
    const int margin = l.screen.w / 10;
    const int top = l.status.bottom() + s.titlePx * 4;
    const int btnH = std::max(kMinTouchPx, l.screen.h / 12);
    const int gap = s.fontPx / 2;
    struct Item {
        int id;
        const char* label;
        const char* hint;
        bool enabled;
    };
    const Item items[] = {
        {Play, "Play", "vs engine", true},
        {Tactics, "Tactics", "puzzles · M4", false},
        {Games, "Games", "historical games · M5", false},
        {Analyze, "Analyze", "", true},
        {Review, "Review", "failed puzzles · M4", false},
        {SettingsItem, "Settings", "", true},
        {Diagnostics, "Diagnostics", "", true},
    };
    int y = top;
    for (const Item& it : items) {
        Button b;
        b.id = it.id;
        b.label = it.label;
        b.hint = it.hint;
        b.enabled = it.enabled;
        b.rect = {margin, y, l.screen.w - 2 * margin, btnH};
        buttons_.push_back(b);
        y += btnH + gap;
    }
    footer_ = {0, l.screen.h - s.fontPx * 3, l.screen.w, s.fontPx * 3};
    for (size_t i = 0; i < buttons_.size(); ++i)
        if (buttons_[i].id == Analyze) focus_ = static_cast<int>(i);
}

void HomeScreen::onEnter() {
    // Battery and engine status only update on screen entry (no animated status area, §6.1).
    app_.invalidate(footer_, RefreshHint::Content);
}

Rect HomeScreen::focusRect(int index) const {
    const int s = app_.style().stroke;
    return buttons_[static_cast<size_t>(index)].rect.inset(-s * 4);
}

void HomeScreen::draw(Canvas& c, const Rect& clip) {
    const BoardTheme& t = app_.theme();
    const UiStyle& s = app_.style();
    const Layout& l = app_.layout();
    c.fill(clip, t.background);
    drawStatusBar(app_, c, "PocketChess", batteryText(app_), "");

    Font& big = app_.fonts().get(s.titlePx * 2, FontStyle::Bold);
    const int titleY = l.status.bottom() + s.titlePx * 2 + big.ascent() / 2;
    const std::string title = "♞ PocketChess";
    big.draw(c, (l.screen.w - big.measure(title)) / 2, titleY, title, t.ink);

    for (size_t i = 0; i < buttons_.size(); ++i) {
        drawButton(c, buttons_[i], t, app_.fonts(), s);
        if (static_cast<int>(i) == focus_) c.frame(focusRect(focus_), s.stroke * 2, t.frame);
    }

    Font& small = app_.fonts().get(s.smallPx);
    const std::string battery = batteryText(app_);
    const std::string left = battery.empty() ? "" : "Battery " + battery;
    const std::string right = "Engine: off";
    const int baseline = footer_.y + footer_.h / 2 + small.ascent() / 2;
    const int pad = s.fontPx;
    small.draw(c, pad, baseline, left, t.ink);
    small.draw(c, l.screen.w - pad - small.measure(right), baseline, right, t.ink);
    const std::string version = "0.0.1 · M3 preview";
    small.draw(c, (l.screen.w - small.measure(version)) / 2, baseline, version, blend(t.background, t.ink, 140));
}

void HomeScreen::open(int id) {
    switch (id) {
    case Play: startPlay(); break;
    case Analyze: app_.push(std::make_unique<AnalyzeScreen>(app_, "")); break;
    case SettingsItem: app_.push(std::make_unique<SettingsScreen>(app_)); break;
    case Diagnostics: app_.push(std::make_unique<DiagnosticsScreen>(app_)); break;
    default: break;
    }
}

void HomeScreen::startPlay() {
    app_.push(std::make_unique<MessageDialog>(
        app_, "Play as", "", std::vector<std::string>{"White", "Black", "Random"},
        [this](int side) {
            if (side < 0) return;
            chess::Color human = side == 0 ? chess::White : chess::Black;
            if (side == 2) human = app_.random().below(2) == 0 ? chess::White : chess::Black;
            const std::vector<std::string>& labels = engine::strengthLabels();
            std::vector<std::string> texts;
            for (const std::string& l : labels) {
                std::string t = l;
                t[0] = static_cast<char>(t[0] - 'a' + 'A');
                texts.push_back((l == app_.settings().playStrength ? "✓ " : "") + t);
            }
            app_.push(std::make_unique<MessageDialog>(
                app_, "Strength", "", texts,
                [this, human](int i) {
                    if (i < 0) return;
                    PlaySetup setup;
                    setup.human = human;
                    setup.strength = engine::strengthLabels()[static_cast<size_t>(i)];
                    if (app_.settings().playStrength != setup.strength) {
                        app_.settings().playStrength = setup.strength;
                        app_.saveSettings();
                    }
                    app_.push(std::make_unique<PlayScreen>(app_, setup));
                },
                true));
        },
        true));
}

void HomeScreen::moveFocus(int delta) {
    const int n = static_cast<int>(buttons_.size());
    int f = focus_;
    for (int k = 0; k < n; ++k) {
        f = (f + delta + n) % n;
        if (buttons_[static_cast<size_t>(f)].enabled) break;
    }
    if (f == focus_) return;
    app_.invalidate(focusRect(focus_), RefreshHint::Selection);
    focus_ = f;
    app_.invalidate(focusRect(focus_), RefreshHint::Selection);
}

void HomeScreen::onGesture(const Gesture& g) {
    if (g.type != Gesture::Type::Tap) return;
    const int id = hitTest(buttons_, g.start);
    if (id >= 0) open(id);
}

void HomeScreen::onKey(const InputEvent& e) {
    switch (e.key) {
    case KeyCode::Prev: case KeyCode::Up: case KeyCode::Left: moveFocus(-1); break;
    case KeyCode::Next: case KeyCode::Down: case KeyCode::Right: moveFocus(1); break;
    case KeyCode::Ok: case KeyCode::Menu: open(buttons_[static_cast<size_t>(focus_)].id); break;
    case KeyCode::Back: app_.host().quit(); break;
    default: break;
    }
}

} // namespace pchess::app
