// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Application.h"

#include <string>

namespace pchess::app {

// Shared screen chrome, the same on every screen (navigation rules, ADR 0014):
// - Top bar: the title in the middle, status text at the right. No controls (one-hand use).
// - Bottom bar: "←" (back to the screen underneath, its name as caption) at the far left, then ◀ ▶
//   doing what the Prev/Next keys do on that screen, the screen's own actions, Menu at the right
//   (the Menu key). The reader's Back/Home keys never reach the app (R7).
// - Every menu ends with "← Back to <screen underneath>" and "Close"; every choice list ends with
//   "Close" (nothing forces a choice).
void drawStatusBar(Application& app, ui::Canvas& c, const Screen& self, const std::string& title,
                   const std::string& right);
// The "←" button in `r`, captioned with the screen underneath (or `override`, for a level inside
// the screen such as an opening family that goes back to the screen itself).
void drawBackButton(Application& app, ui::Canvas& c, const Screen& self, const ui::Rect& r,
                    const std::string& override = {});
// "← Back to Games" for the screen under `self`.
std::string backMenuLabel(const Application& app, const Screen& self);
std::string batteryText(Application& app);

} // namespace pchess::app
