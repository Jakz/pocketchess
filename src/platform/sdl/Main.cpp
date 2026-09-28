// SPDX-License-Identifier: GPL-3.0-or-later
// PocketChess.exe: desktop build with the e-ink simulator (build doc §3).
#include "app/Application.h"
#include "app/CommandLine.h"
#include "app/Screenshot.h"
#include "app/Script.h"
#include "core/util/Log.h"
#include "core/util/Paths.h"
#include "platform/sdl/SdlPlatform.h"

#include <SDL3/SDL_main.h>

#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main(int argc, char** argv) {
    using namespace pchess;
    app::CommandLine cl = app::CommandLine::parse(argc, argv);
    if (cl.help || !cl.error.empty()) {
        if (!cl.error.empty()) std::fprintf(stderr, "%s\n", cl.error.c_str());
        std::fprintf(stderr, "%s", app::CommandLine::usage());
        return cl.help ? 0 : 2;
    }

    const char* base = SDL_GetBasePath();  // the exe directory, owned by SDL
    const std::filesystem::path exeDir = base ? std::filesystem::path(base) : std::filesystem::current_path();
    cl.app.root = util::Paths::resolveRoot(cl.root, exeDir);

    if (!cl.screenshot.empty()) return app::renderScreenshot(cl.app, cl.profile, cl.script, cl.screenshot);

    util::Log::setTimingEnabled(cl.timing);
#if defined(_WIN32)
    util::Log::setSink([](util::LogLevel, std::string_view line) {
        std::string s(line);
        s += '\n';
        OutputDebugStringA(s.c_str());
    });
#else
    util::Log::setSink([](util::LogLevel, std::string_view line) { std::fprintf(stderr, "%.*s\n", static_cast<int>(line.size()), line.data()); });
#endif

    platform::sdl::SdlHost host;
    std::string error;
    if (!host.init(cl.profile, cl.scale, error)) {
        std::fprintf(stderr, "PocketChess: cannot open the window: %s\n", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PocketChess", error.c_str(), nullptr);
        return 1;
    }
    host.presenter().simulator().loadParams(cl.app.root / "simulator" / "kaleido3.yaml");

    app::Application application(host, host.presenter(), cl.app);
    application.start();
    if (!cl.script.empty()) {
        if (auto r = app::runScript(application, cl.script); !r) util::Log::warn("app", r.error().message());
    }
    const int code = host.run(application);
    util::Log::info("app", "exit");
    util::Log::closeFile();
    return code;
}
