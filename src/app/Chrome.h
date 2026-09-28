// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Application.h"

#include <string>

namespace pchess::app {

// Shared screen chrome: the top status bar with an on-screen back button (R7: Back/Home keys
// may not reach the app, so every screen has a visible way back).
ui::Rect statusBackRect(const Application& app);
void drawStatusBar(Application& app, ui::Canvas& c, const std::string& title, const std::string& right,
                   const std::string& backLabel);
std::string batteryText(Application& app);

} // namespace pchess::app
