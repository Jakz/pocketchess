// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme/BoardTheme.h"

namespace pchess::ui {

// Kaleido 3 is muted: moderate saturation, strong luminance contrast between squares.
BoardTheme BoardTheme::paper() {
    BoardTheme t;
    t.name = "Paper";
    t.light = Rgb::hex(0xE8DFC9);
    t.dark = Rgb::hex(0x8F9B72);
    t.selected = Rgb::hex(0xC8B95B);
    t.lastMove = Rgb::hex(0xD2C47A);
    t.check = Rgb::hex(0xB56A61);
    t.marker = Rgb::hex(0x3A3A32);
    t.coordOnLight = Rgb::hex(0x6E7656);
    t.coordOnDark = Rgb::hex(0xF1EBDC);
    t.background = Rgb::hex(0xFAF8F2);
    t.ink = Rgb::hex(0x1E1E1A);
    t.frame = Rgb::hex(0x3A3A32);
    return t;
}

BoardTheme BoardTheme::slate() {
    BoardTheme t = paper();
    t.name = "Slate";
    t.light = Rgb::hex(0xDEE3E6);
    t.dark = Rgb::hex(0x7D8FA0);
    t.selected = Rgb::hex(0xB9C77A);
    t.lastMove = Rgb::hex(0xC9D38E);
    t.check = Rgb::hex(0xB86A6A);
    t.coordOnLight = Rgb::hex(0x5E6F80);
    t.coordOnDark = Rgb::hex(0xEEF1F3);
    return t;
}

BoardTheme BoardTheme::classicMono() {
    BoardTheme t;
    t.name = "ClassicMono";
    t.mono = true;
    t.light = Rgb::gray(255);
    t.dark = Rgb::gray(150);
    t.selected = Rgb::gray(0);
    t.lastMove = Rgb::gray(0);
    t.check = Rgb::gray(0);
    t.marker = Rgb::gray(40);
    t.coordOnLight = Rgb::gray(90);
    t.coordOnDark = Rgb::gray(255);
    t.background = Rgb::gray(255);
    t.ink = Rgb::gray(0);
    t.frame = Rgb::gray(0);
    t.selectedStyle = MarkerStyle::Hatch;
    t.lastMoveStyle = MarkerStyle::Dots;
    t.checkStyle = MarkerStyle::DoubleBorder;
    t.tintMarkers = false;
    return t;
}

BoardTheme BoardTheme::highContrastMono() {
    BoardTheme t = classicMono();
    t.name = "HighContrastMono";
    t.dark = Rgb::gray(110);
    t.coordOnLight = Rgb::gray(0);
    t.selectedStyle = MarkerStyle::Outline;
    return t;
}

std::vector<BoardTheme> BoardTheme::builtIns() { return {paper(), slate(), classicMono(), highContrastMono()}; }

BoardTheme BoardTheme::byName(const std::string& name) {
    for (auto& t : builtIns())
        if (t.name == name) return t;
    return paper();
}

} // namespace pchess::ui
