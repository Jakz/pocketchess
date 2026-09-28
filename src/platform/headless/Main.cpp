// SPDX-License-Identifier: GPL-3.0-or-later
// pocketchess-headless: renders screenshots without a window (any OS, CI, docs).
//   pocketchess-headless --root <dir> --screen analyze --script "tap:e2;tap:e4" --screenshot out.png
#include "app/CommandLine.h"
#include "app/Screenshot.h"
#include "core/util/Paths.h"

#include <cstdio>

int main(int argc, char** argv) {
    using namespace pchess;
    app::CommandLine cl = app::CommandLine::parse(argc, argv);
    if (cl.help || !cl.error.empty() || cl.screenshot.empty()) {
        if (!cl.error.empty()) std::fprintf(stderr, "%s\n", cl.error.c_str());
        std::fprintf(stderr, "%s", app::CommandLine::usage());
        if (cl.screenshot.empty() && !cl.help) std::fprintf(stderr, "--screenshot is required here\n");
        return cl.help ? 0 : 2;
    }
    std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path();
    cl.app.root = util::Paths::resolveRoot(cl.root, exeDir);
    return app::renderScreenshot(cl.app, cl.profile, cl.script, cl.screenshot);
}
