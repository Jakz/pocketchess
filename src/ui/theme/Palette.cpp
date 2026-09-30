// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme/Palette.h"

#include <cstdlib>

namespace pchess::ui {

const std::vector<NamedColor>& accentColors() {
    static const std::vector<NamedColor> colors = {
        {"Graphite", Rgb::hex(0x2B2B2B)}, {"Red", Rgb::hex(0xC62828)},    {"Orange", Rgb::hex(0xE65100)},
        {"Amber", Rgb::hex(0xB77800)},    {"Green", Rgb::hex(0x2E7D32)},  {"Teal", Rgb::hex(0x00796B)},
        {"Blue", Rgb::hex(0x1565C0)},     {"Indigo", Rgb::hex(0x3949AB)}, {"Purple", Rgb::hex(0x7B1FA2)},
        {"Pink", Rgb::hex(0xC2185B)},     {"Brown", Rgb::hex(0x6D4C41)},
    };
    return colors;
}

std::optional<Rgb> findAccent(const std::string& name) {
    for (const NamedColor& c : accentColors())
        if (c.name == name) return c.color;
    return std::nullopt;
}

Rgb textOn(const BoardTheme& t, Rgb fill) {
    const int d1 = std::abs(static_cast<int>(fill.luma()) - static_cast<int>(t.ink.luma()));
    const int d2 = std::abs(static_cast<int>(fill.luma()) - static_cast<int>(t.background.luma()));
    return d1 >= d2 ? t.ink : t.background;
}

void applyAccent(BoardTheme& t, Rgb accent) {
    t.accent = accent;
    t.selected = blend(t.light, accent, 150);
    t.lastMove = blend(t.light, accent, 90);
}

void applyPieceTint(BoardTheme& t, Rgb hue) {
    t.tintPieces = true;
    t.whitePiece = blend(Rgb::gray(255), hue, 80);
    t.blackPiece = blend(Rgb::gray(0), hue, 150);
}

Bitmap recolor(const Bitmap& piece, Rgb dark, Rgb light) {
    Bitmap out = piece;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            uint8_t* p = out.px(x, y);
            if (p[3] == 0) continue;
            const Rgb c = blend(dark, light, Rgb{p[0], p[1], p[2]}.luma());
            p[0] = c.r, p[1] = c.g, p[2] = c.b;
        }
    return out;
}

} // namespace pchess::ui
