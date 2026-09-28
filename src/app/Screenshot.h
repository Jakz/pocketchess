// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Application.h"

#include <filesystem>
#include <string>

namespace pchess::app {

// Renders the app headlessly (optionally after a script) and writes the canvas PNG
// (pre-simulation pixels, architecture doc §4). Used by `--screenshot` on every platform.
// Returns a process exit code.
int renderScreenshot(const AppConfig& config, const platform::DeviceProfile& profile, const std::string& script,
                     const std::filesystem::path& out);

} // namespace pchess::app
