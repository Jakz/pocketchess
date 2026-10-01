// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Chrome.h"

#include <algorithm>

namespace pchess::app {

using namespace ui;

void drawStatusBar(Application& app, Canvas& c, const Screen&, const std::string& title, const std::string& right) {
    const BoardTheme& t = app.theme();
    const UiStyle& st = app.style();
    const Rect s = app.layout().status;
    if (!s.intersects(c.clip())) return;
    c.fill(s, t.background);
    c.fill({s.x, s.bottom() - st.stroke, s.w, st.stroke}, t.frame);
    Font& f = app.fonts().get(st.fontPx, FontStyle::Bold);
    Font& small = app.fonts().get(st.smallPx);
    const int pad = st.fontPx / 2;
    const int rightW = right.empty() ? 0 : small.measure(right) + pad;
    const int titleW = s.w - 2 * std::max(rightW, s.w / 6);
    f.drawCentered(c, {s.x + (s.w - titleW) / 2, s.y, titleW, s.h - st.stroke}, fitText(f, title, titleW), t.ink);
    if (!right.empty())
        small.draw(c, s.right() - pad - small.measure(right),
                   s.y + (s.h - small.ascent() - small.descent()) / 2 + small.ascent(), right, t.ink);
}

void drawBackButton(Application& app, Canvas& c, const Screen& self, const Rect& r, const std::string& override) {
    if (r.empty() || !r.intersects(c.clip())) return;
    Button b;
    b.label = "←";
    b.hint = override.empty() ? app.parentTitle(&self) : override;
    if (b.hint.empty()) b.hint = "Home";
    b.rect = r;
    drawButton(c, b, app.theme(), app.fonts(), app.style());
}

std::string backMenuLabel(const Application& app, const Screen& self) {
    const std::string back = app.parentTitle(&self);
    return "← Back to " + (back.empty() ? std::string("Home") : back);
}

std::string batteryText(Application& app) {
    const int b = app.host().power().batteryPercent();
    if (b < 0) return {};
    return std::to_string(b) + "%" + (app.host().power().charging() ? " +" : "");
}

} // namespace pchess::app
