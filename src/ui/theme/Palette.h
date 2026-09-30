// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Canvas.h"
#include "ui/theme/BoardTheme.h"

#include <optional>
#include <string>
#include <vector>

namespace pchess::ui {

// Named hues for the accent and piece-color pickers. Kaleido 3 shows color muted and at half
// resolution, so these are strongly saturated and dark enough to carry white text.
struct NamedColor {
    std::string name;
    Rgb color;
};
const std::vector<NamedColor>& accentColors();
std::optional<Rgb> findAccent(const std::string& name);

// Ink or background, whichever reads better on `fill`.
Rgb textOn(const BoardTheme& t, Rgb fill);

// The accent drives primary buttons, the current move and the selected/last-move squares.
void applyAccent(BoardTheme& t, Rgb accent);
// White pieces in a pale tint of `hue`, black pieces in a deep shade of it.
void applyPieceTint(BoardTheme& t, Rgb hue);

// Recolors a black-and-white piece bitmap: luma 0 -> `dark`, 255 -> `light`, alpha kept.
Bitmap recolor(const Bitmap& piece, Rgb dark, Rgb light);

} // namespace pchess::ui
