// SPDX-License-Identifier: GPL-3.0-or-later
// PocketChessTests: runs every suite. Options:
//   --filter <text>     only tests whose name contains <text>
//   --slow              include tests tagged [slow] (deep perft)
//   --integration       include tests tagged [integration] (real engine binaries)
//   --update-golden     rewrite golden images instead of comparing
//   --golden-dir <dir>  tests/golden (default: searched upwards from the working directory)
//   --font-dir <dir>    data/fonts   (default: searched upwards)
//   --verbose           print every test name and time
//   --bench-movegen     perft micro-benchmark (architecture doc §7) and exit
//   --fake-engine       act as a scripted UCI engine on stdin/stdout (used by the process tests)
#include "support/Test.h"
#include "support/TestTools.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

std::string findUp(const std::filesystem::path& rel) {
    std::filesystem::path p = std::filesystem::current_path();
    for (int up = 0; up < 8; ++up) {
        if (std::filesystem::exists(p / rel)) return (p / rel).string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    using namespace pchess::test;
    if (argc >= 2 && std::string(argv[1]) == "--fake-engine") return pchess::test::fakeEngine();
    if (argc >= 2 && std::string(argv[1]) == "--bench-movegen") return pchess::test::benchMovegen();

    RunOptions opt;
    bool verbose = false;
    std::string golden, fonts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--filter" && i + 1 < argc) opt.filter = argv[++i];
        else if (a == "--slow") opt.includeSlow = true;
        else if (a == "--integration") opt.includeIntegration = true;
        else if (a == "--update-golden") setUpdateGolden(true);
        else if (a == "--golden-dir" && i + 1 < argc) golden = argv[++i];
        else if (a == "--font-dir" && i + 1 < argc) fonts = argv[++i];
        else if (a == "--verbose") verbose = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (golden.empty()) golden = findUp("tests/golden");
    if (fonts.empty()) fonts = findUp("data/fonts");
    setGoldenDir(golden.empty() ? "tests/golden" : golden);
    setFontDir(fonts.empty() ? "data/fonts" : fonts);
    if (const std::string fixtures = findUp("tests/fixtures"); !fixtures.empty()) setFixtureDir(fixtures);
    std::filesystem::path self = std::filesystem::absolute(argv[0]);
    if (!self.has_extension() && std::filesystem::exists(self.string() + ".exe")) self += ".exe";  // Windows
    setSelfExe(self.string());

    Reporter rep;
    rep.onStart = [&](const TestCase& tc) {
        if (verbose) std::printf("[ RUN  ] %s\n", tc.name);
    };
    rep.onFailure = [](const TestCase& tc, const std::string& msg) {
        std::printf("[ FAIL ] %s\n         %s\n", tc.name, msg.c_str());
    };
    rep.onEnd = [&](const TestCase& tc, bool passed, double ms) {
        if (verbose && passed) std::printf("[  OK  ] %s (%.1f ms)\n", tc.name, ms);
    };
    rep.onMessage = [](const std::string& m) { std::printf("         %s\n", m.c_str()); };

    const Summary s = runTests(opt, rep);
    std::printf("%d passed, %d failed, %d skipped, %d assertions\n", s.passed, s.failed, s.skipped, s.assertions);
    std::fflush(stdout);
    return s.failed == 0 ? 0 : 1;
}
