// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CommandLine.h"

#include <cstdlib>

namespace pchess::app {

CommandLine CommandLine::parse(int argc, char** argv) {
    CommandLine cl;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                cl.error = "missing value for " + a;
                return {};
            }
            return argv[++i];
        };
        if (a == "--scale") cl.scale = std::atof(value().c_str());
        else if (a == "--profile") {
            const std::string name = value();
            if (auto p = platform::DeviceProfile::byName(name)) cl.profile = *p;
            else cl.error = "unknown profile " + name;
        } else if (a == "--root") cl.root = std::filesystem::path(value());
        else if (a == "--screen") cl.app.startScreen = value();
        else if (a == "--fen") cl.app.fen = value();
        else if (a == "--mono") cl.app.mono = true;
        else if (a == "--color") cl.app.mono = false;
        else if (a == "--theme") cl.app.theme = value();
        else if (a == "--screenshot") cl.screenshot = value();
        else if (a == "--script") cl.script = value();
        else if (a == "--timing") cl.timing = true;
        else if (a == "--help" || a == "-h" || a == "/?") cl.help = true;
        else if (a.rfind("-psn", 0) == 0) continue;  // ignore OS-provided arguments
        else cl.error = "unknown option " + a;
        if (!cl.error.empty()) break;
    }
    if (cl.app.startScreen.empty()) cl.app.startScreen = "home";
    if (!cl.app.fen.empty() && cl.app.startScreen == "home") cl.app.startScreen = "analyze";
    return cl;
}

const char* CommandLine::usage() {
    return "PocketChess options:\n"
           "  --scale <f>            window scale (desktop; default fits the screen)\n"
           "  --profile <name>       verse-pro-color | mono-758x1024 | mono-1072x1448 | era-1264x1680\n"
           "  --root <dir>           data root (default <exe dir>/pocketchess or %POCKETCHESS_ROOT%)\n"
           "  --screen <name>        home | analyze | settings | diagnostics\n"
           "  --fen \"<fen>\"          start the Analyze screen on this position\n"
           "  --mono | --color       display mode\n"
           "  --theme <name>         Paper | Slate | ClassicMono | HighContrastMono\n"
           "  --script \"<cmds>\"      scripted input, e.g. \"tap:e2;tap:e4;key:prev\"\n"
           "  --screenshot <out.png> render one frame (after --script) and exit\n"
           "  --timing               log present timings\n";
}

} // namespace pchess::app
