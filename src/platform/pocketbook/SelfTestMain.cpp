// SPDX-License-Identifier: GPL-3.0-or-later
// pocketchess-selftest.app: the desktop test suites on the real CPU (32-bit ARM, unsigned char,
// the device libc/libm), plus golden-image checksums (testing doc §1, P9) and the [integration]
// tests against the bundled engines (ADR 0009: static hard-float binaries). Results are shown on
// screen and written to <root>/logs/selftest.log. Any key closes the app.
#include "core/util/Log.h"
#include "core/util/Paths.h"
#include "support/Test.h"
#include "support/TestTools.h"
#include "ui/canvas/Canvas.h"
#include "ui/canvas/Font.h"
#include "ui/widgets/Widgets.h"

#include <inkview.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using namespace pchess;

util::Paths* g_paths = nullptr;
std::vector<std::string> g_lines;
bool g_ran = false;

std::filesystem::path selfPath() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "/mnt/ext1/applications/pocketchess-selftest.app";
    buf[n] = 0;
    return buf;
}

void show(const std::string& title, const std::vector<std::string>& lines) {
    const int w = ScreenWidth(), h = ScreenHeight();
    ui::Canvas c(w, h);
    ui::FontLibrary fonts;
    (void)fonts.load(g_paths->fonts() / "DejaVuSans.ttf", g_paths->fonts() / "DejaVuSans-Bold.ttf");
    c.fill(c.bounds(), ui::colors::White);
    ui::Font& big = fonts.get(44, ui::FontStyle::Bold);
    big.draw(c, 30, 80, title, ui::colors::Black);
    ui::Font& f = fonts.get(24);
    ui::drawLines(c, {30, 120, w - 60, h - 150}, lines, f, ui::colors::Black);
    Stretch(c.data(), IMAGE_RGB, w, h, c.stride(), 0, 0, w, h, 0);
    FullUpdate();
}

void runAll() {
    using namespace pchess::test;
    setGoldenDir((g_paths->root() / "selftest").string());
    setFontDir(g_paths->fonts().string());
    setFixtureDir((g_paths->root() / "selftest" / "fixtures").string());
    setSelfExe(selfPath().string());

    std::FILE* log = std::fopen((g_paths->logs() / "selftest.log").string().c_str(), "w");
    Reporter rep;
    rep.onFailure = [&](const TestCase& tc, const std::string& msg) {
        g_lines.push_back(std::string("FAIL ") + tc.name);
        g_lines.push_back("  " + msg.substr(msg.find_last_of('/') == std::string::npos ? 0 : msg.find_last_of('/') + 1));
        if (log) std::fprintf(log, "FAIL %s\n  %s\n", tc.name, msg.c_str());
    };
    rep.onEnd = [&](const TestCase& tc, bool passed, double ms) {
        if (log) std::fprintf(log, "%s %s (%.1f ms)\n", passed ? "ok  " : "FAIL", tc.name, ms);
    };
    rep.onMessage = [&](const std::string& m) {
        if (log) std::fprintf(log, "     %s\n", m.c_str());
    };
    show("PocketChess self-test",
         {"Running...", "Perft, FEN/SAN/PGN, UCI, canvas, golden images, installed engines."});
    RunOptions opt;  // [slow] stays off; [integration] runs the bundled engines on the device CPU
    opt.includeIntegration = true;
    const Summary s = runTests(opt, rep);
    char summary[128];
    std::snprintf(summary, sizeof summary, "%d passed, %d failed, %d skipped, %d assertions", s.passed, s.failed,
                  s.skipped, s.assertions);
    if (log) {
        std::fprintf(log, "%s\n", summary);
        std::fclose(log);
    }
    g_lines.insert(g_lines.begin(), summary);
    g_lines.push_back("");
    g_lines.push_back("Log: " + (g_paths->logs() / "selftest.log").string());
    g_lines.push_back("Press any key to exit.");
    show(s.failed == 0 ? "Self-test PASSED" : "Self-test FAILED", g_lines);
}

int handler(int type, int, int) {
    switch (type) {
    case EVT_SHOW:
        if (!g_ran) {
            g_ran = true;
            runAll();
        }
        return 1;
    case EVT_KEYPRESS:
        CloseApp();
        return 1;
    default: return 0;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--fake-engine") == 0) return pchess::test::fakeEngine();
    if (argc >= 2 && std::strcmp(argv[1], "--bench-movegen") == 0) return pchess::test::benchMovegen();
    // The self-test shares the app's data folder: <applications>/pocketchess/.
    static util::Paths paths(util::Paths::resolveRoot(std::nullopt, selfPath().parent_path()));
    paths.ensureWritableDirs();
    g_paths = &paths;
    InkViewMain(handler);
    return 0;
}
