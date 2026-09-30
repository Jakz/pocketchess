// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Canvas.h"
#include "ui/canvas/Font.h"
#include "ui/theme/BoardTheme.h"

#include <string>
#include <vector>

namespace pchess::ui {

// Minimum touch target: 9 mm (design notes §39.3) = 106 px at 300 ppi.
constexpr int kMinTouchPx = 106;

struct Button {
    Rect rect;
    std::string label;
    int id = 0;
    bool enabled = true;
    bool primary = false;
    std::string hint;  // small second line, e.g. "M3"
};

struct UiStyle {
    int fontPx = 34;
    int smallPx = 26;
    int titlePx = 40;
    int stroke = 3;
    int radius = 0;
};

void drawButton(Canvas& c, const Button& b, const BoardTheme& t, FontLibrary& fonts, const UiStyle& s);
// Returns the id of the enabled button under p, or -1.
int hitTest(const std::vector<Button>& buttons, Point p);
// Splits `area` into `count` equal cells horizontally with `gap` between them.
std::vector<Rect> splitRow(const Rect& area, int count, int gap);
std::vector<Rect> splitColumn(const Rect& area, int count, int gap);

// Draws text lines top-aligned inside r, clipped; returns the y after the last line.
int drawLines(Canvas& c, const Rect& r, const std::vector<std::string>& lines, Font& font, Rgb color);

// `text` shortened with "…" (on a UTF-8 character boundary) to fit `width` pixels.
std::string fitText(Font& font, const std::string& text, int width);

} // namespace pchess::ui
