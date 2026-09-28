// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/Test.h"

#include <chrono>
#include <exception>

namespace pchess::test {
namespace {

struct RunState {
    const TestCase* current = nullptr;
    const Reporter* reporter = nullptr;
    bool failed = false;
    int assertions = 0;
};

RunState& state() {
    static RunState s;
    return s;
}

bool g_updateGolden = false;
std::string g_goldenDir = "tests/golden";
std::string g_fontDir = "data/fonts";
std::string g_selfExe;

} // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

void reportFailure(const char* file, int line, const std::string& message) {
    auto& s = state();
    s.failed = true;
    std::string text = std::string(file) + ":" + std::to_string(line) + ": " + message;
    if (s.reporter && s.reporter->onFailure && s.current) s.reporter->onFailure(*s.current, text);
}

void countAssertion() { ++state().assertions; }

void message(const std::string& text) {
    auto& s = state();
    if (s.reporter && s.reporter->onMessage) s.reporter->onMessage(text);
}

Summary runTests(const RunOptions& options, const Reporter& reporter) {
    Summary sum;
    auto& s = state();
    s.reporter = &reporter;
    s.assertions = 0;
    for (const TestCase& tc : registry()) {
        const std::string_view name = tc.name;
        const bool slow = name.find("[slow]") != std::string_view::npos;
        const bool integration = name.find("[integration]") != std::string_view::npos;
        if ((!options.filter.empty() && name.find(options.filter) == std::string_view::npos) ||
            (slow && !options.includeSlow) || (integration && !options.includeIntegration)) {
            ++sum.skipped;
            continue;
        }
        s.current = &tc;
        s.failed = false;
        if (reporter.onStart) reporter.onStart(tc);
        const auto t0 = std::chrono::steady_clock::now();
        try {
            tc.fn();
        } catch (const RequireFailed&) {
            // already reported
        } catch (const std::exception& e) {
            reportFailure(tc.file, tc.line, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            reportFailure(tc.file, tc.line, "unexpected non-standard exception");
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (s.failed) ++sum.failed;
        else ++sum.passed;
        if (reporter.onEnd) reporter.onEnd(tc, !s.failed, ms);
    }
    sum.assertions = s.assertions;
    s.current = nullptr;
    s.reporter = nullptr;
    return sum;
}

bool updateGolden() { return g_updateGolden; }
void setUpdateGolden(bool value) { g_updateGolden = value; }
const std::string& goldenDir() { return g_goldenDir; }
void setGoldenDir(std::string dir) { g_goldenDir = std::move(dir); }
const std::string& fontDir() { return g_fontDir; }
void setFontDir(std::string dir) { g_fontDir = std::move(dir); }
const std::string& selfExe() { return g_selfExe; }
void setSelfExe(std::string path) { g_selfExe = std::move(path); }

} // namespace pchess::test
