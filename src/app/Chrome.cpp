// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Chrome.h"

namespace pchess::app {

using namespace ui;

Rect statusBackRect(const Application& app) {
    const Rect s = app.layout().status;
    return {s.x, s.y, s.w / 4, s.h};
}

void drawStatusBar(Application& app, Canvas& c, const std::string& title, const std::string& right,
                   const std::string& backLabel) {
    const BoardTheme& t = app.theme();
    const UiStyle& st = app.style();
    const Rect s = app.layout().status;
    if (!s.intersects(c.clip())) return;
    c.fill(s, t.background);
    c.fill({s.x, s.bottom() - st.stroke, s.w, st.stroke}, t.frame);
    Font& f = app.fonts().get(st.fontPx, FontStyle::Bold);
    Font& small = app.fonts().get(st.smallPx);
    const int pad = st.fontPx / 2;
    if (!backLabel.empty()) {
        const Rect b = statusBackRect(app);
        small.draw(c, b.x + pad, b.y + (b.h - small.ascent() - small.descent()) / 2 + small.ascent(),
                   "‹ " + backLabel, t.ink);
    }
    f.drawCentered(c, {s.x + s.w / 4, s.y, s.w / 2, s.h - st.stroke}, title, t.ink);
    if (!right.empty()) {
        const int w = small.measure(right);
        small.draw(c, s.right() - pad - w, s.y + (s.h - small.ascent() - small.descent()) / 2 + small.ascent(), right, t.ink);
    }
}

std::string batteryText(Application& app) {
    const int b = app.host().power().batteryPercent();
    if (b < 0) return {};
    return std::to_string(b) + "%" + (app.host().power().charging() ? " +" : "");
}

} // namespace pchess::app
