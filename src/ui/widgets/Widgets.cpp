// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/Widgets.h"

namespace pchess::ui {

void drawButton(Canvas& c, const Button& b, const BoardTheme& t, FontLibrary& fonts, const UiStyle& s) {
    c.pushClip(b.rect);
    const Rgb bg = b.primary ? t.ink : t.background;
    const Rgb fg = !b.enabled ? blend(t.background, t.ink, 110) : (b.primary ? t.background : t.ink);
    c.fill(b.rect, bg);
    c.frame(b.rect, b.enabled ? s.stroke : std::max(1, s.stroke - 1), b.enabled ? t.frame : fg);
    Font& f = fonts.get(s.fontPx, b.primary ? FontStyle::Bold : FontStyle::Regular);
    if (b.hint.empty()) {
        f.drawCentered(c, b.rect, b.label, fg);
    } else {
        Font& small = fonts.get(s.smallPx);
        const int total = f.ascent() + f.descent() + small.ascent() + small.descent();
        const int top = b.rect.y + (b.rect.h - total) / 2;
        f.draw(c, b.rect.x + (b.rect.w - f.measure(b.label)) / 2, top + f.ascent(), b.label, fg);
        small.draw(c, b.rect.x + (b.rect.w - small.measure(b.hint)) / 2,
                   top + f.ascent() + f.descent() + small.ascent(), b.hint, fg);
    }
    c.popClip();
}

int hitTest(const std::vector<Button>& buttons, Point p) {
    for (const Button& b : buttons)
        if (b.enabled && b.rect.contains(p)) return b.id;
    return -1;
}

std::vector<Rect> splitRow(const Rect& area, int count, int gap) {
    std::vector<Rect> out;
    if (count <= 0) return out;
    const int w = (area.w - gap * (count - 1)) / count;
    int x = area.x;
    for (int i = 0; i < count; ++i) {
        const int ww = i == count - 1 ? area.right() - x : w;
        out.push_back({x, area.y, ww, area.h});
        x += w + gap;
    }
    return out;
}

std::vector<Rect> splitColumn(const Rect& area, int count, int gap) {
    std::vector<Rect> out;
    if (count <= 0) return out;
    const int h = (area.h - gap * (count - 1)) / count;
    int y = area.y;
    for (int i = 0; i < count; ++i) {
        const int hh = i == count - 1 ? area.bottom() - y : h;
        out.push_back({area.x, y, area.w, hh});
        y += h + gap;
    }
    return out;
}

int drawLines(Canvas& c, const Rect& r, const std::vector<std::string>& lines, Font& font, Rgb color) {
    c.pushClip(r);
    int y = r.y;
    for (const std::string& line : lines) {
        if (y >= r.bottom()) break;
        font.draw(c, r.x, y + font.ascent(), line, color);
        y += font.lineHeight();
    }
    c.popClip();
    return y;
}

} // namespace pchess::ui
