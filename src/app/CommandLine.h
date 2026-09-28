// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Application.h"

#include <filesystem>
#include <optional>
#include <string>

namespace pchess::app {

// Shared command line of the desktop, headless and device front ends (build doc §3.2):
//   --scale <f> --profile <name> --root <dir> --screen <name> --fen "<fen>" --mono --theme <name>
//   --screenshot <out.png> --script "<commands>" --timing --help
struct CommandLine {
    AppConfig app;
    platform::DeviceProfile profile = platform::DeviceProfile::verseProColor();
    std::optional<std::filesystem::path> root;
    double scale = 0;  // 0 = fit to the desktop
    std::filesystem::path screenshot;
    std::string script;
    bool timing = false;
    bool help = false;
    std::string error;

    static CommandLine parse(int argc, char** argv);
    static const char* usage();
};

} // namespace pchess::app
