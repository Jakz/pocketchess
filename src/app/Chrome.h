// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Application.h"

#include <string>

namespace pchess::app {

// Shared screen chrome, the same on every screen (navigation rules, ADR 0014):
// - Top bar: "‹ <screen underneath>" at the left is the one on-screen way back (the reader's
//   Back/Home keys never reach the app, R7); the title in the middle; status text at the right.
// - Bottom bar: ◀ ▶ at the left do what the Prev/Next keys do on that screen, the screen's own
//   actions in the middle, Menu at the right (the Menu key).
// - Every menu ends with "‹ Back to <screen underneath>" and "Close".
ui::Rect statusBackRect(const Application& app);
void drawStatusBar(Application& app, ui::Canvas& c, const Screen& self, const std::string& title,
                   const std::string& right);
// "‹ Back to Games" for the screen under `self`.
std::string backMenuLabel(const Application& app, const Screen& self);
std::string batteryText(Application& app);

} // namespace pchess::app
