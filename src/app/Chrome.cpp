// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Chrome.h"

namespace pchess::app {

using namespace ui;

Rect statusBackRect(const Application& app) {
    const Rect s = app.layout().status;
    return {s.x, s.y, s.w / 4, s.h};
}

void drawStatusBar(Application& app, Canvas& c, const Screen& self, const std::string& title,
                   const std::string& right) {
    const BoardTheme& t = app.theme();
    const UiStyle& st = app.style();
    const Rect s = app.layout().status;
    if (!s.intersects(c.clip())) return;
    c.fill(s, t.background);
    c.fill({s.x, s.bottom() - st.stroke, s.w, st.stroke}, t.frame);
    Font& f = app.fonts().get(st.fontPx, FontStyle::Bold);
    Font& small = app.fonts().get(st.smallPx);
    const int pad = st.fontPx / 2;
    const std::string back = app.parentTitle(&self);
    if (!back.empty()) {  // a real button: framed, the full height of the bar
        const Rect b = statusBackRect(app).inset(st.stroke * 2);
        c.frame(b, st.stroke, t.frame);
        f.drawCentered(c, b, fitText(f, "‹ " + back, b.w - pad), t.ink);
    }
    f.drawCentered(c, {s.x + s.w / 4, s.y, s.w / 2, s.h - st.stroke}, fitText(f, title, s.w / 2 - pad), t.ink);
    if (!right.empty()) {
        const int w = small.measure(right);
        small.draw(c, s.right() - pad - w, s.y + (s.h - small.ascent() - small.descent()) / 2 + small.ascent(), right, t.ink);
    }
}

std::string backMenuLabel(const Application& app, const Screen& self) {
    const std::string back = app.parentTitle(&self);
    return "‹ Back to " + (back.empty() ? std::string("Home") : back);
}

std::string batteryText(Application& app) {
    const int b = app.host().power().batteryPercent();
    if (b < 0) return {};
    return std::to_string(b) + "%" + (app.host().power().charging() ? " +" : "");
}

} // namespace pchess::app
