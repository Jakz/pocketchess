// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Screenshot.h"

#include "app/HeadlessHost.h"
#include "app/Script.h"
#include "ui/canvas/PngWriter.h"

#include <cstdio>

namespace pchess::app {

int renderScreenshot(const AppConfig& config, const platform::DeviceProfile& profile, const std::string& script,
                     const std::filesystem::path& out) {
    HeadlessHost host;
    HeadlessPresenter presenter(profile);
    AppConfig cfg = config;
    cfg.logToFile = false;
    Application app(host, presenter, cfg);
    app.start();
    if (!script.empty()) {
        auto r = runScript(app, script, [&](int ms) {
            host.advance(std::chrono::milliseconds(ms));
            app.flush();
        });
        if (!r) {
            std::fprintf(stderr, "%s\n", r.error().message().c_str());
            return 2;
        }
    }
    host.advance();
    app.flush();
    if (auto r = ui::writePng(out, app.canvas()); !r) {
        std::fprintf(stderr, "%s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("wrote %s (%dx%d, %zu presents)\n", out.string().c_str(), app.canvas().width(), app.canvas().height(),
                presenter.requests.size());
    return 0;
}

} // namespace pchess::app
