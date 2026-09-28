// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PocketChess test framework: a small doctest-compatible subset (TEST_CASE, CHECK, CHECK_EQ,
// CHECK_FALSE, REQUIRE, REQUIRE_EQ, MESSAGE), with no dependencies so the same suites run in
// PocketChessTests.exe and in the device self-test app (ADR 0005).

#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pchess::test {

struct TestCase {
    const char* name;
    const char* file;
    int line;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, const char* file, int line, void (*fn)()) {
        registry().push_back({name, file, line, fn});
    }
};

struct RequireFailed {};

// Reporting hooks (console for desktop, file + screen for the device self-test).
struct Reporter {
    std::function<void(const TestCase&)> onStart;
    std::function<void(const TestCase&, const std::string& message)> onFailure;
    std::function<void(const TestCase&, bool passed, double ms)> onEnd;
    std::function<void(const std::string& message)> onMessage;
};

struct RunOptions {
    std::string filter;        // substring match on the name; empty = all
    bool includeSlow = false;  // names containing "[slow]" are skipped unless set
    bool includeIntegration = false; // names containing "[integration]"
};

struct Summary {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int assertions = 0;
};

Summary runTests(const RunOptions& options, const Reporter& reporter);

// Used by the macros.
void reportFailure(const char* file, int line, const std::string& message);
void countAssertion();
void message(const std::string& text);

template <class T>
std::string stringify(const T& v) {
    if constexpr (requires(std::ostream& os, const T& x) { os << x; }) {
        std::ostringstream os;
        os << v;
        return os.str();
    } else {
        return "{?}";
    }
}

// Global settings for golden-image and resource tests (set by the runner's main).
bool updateGolden();
void setUpdateGolden(bool value);
const std::string& goldenDir();      // tests/golden (desktop) or <root>/selftest (device)
void setGoldenDir(std::string dir);
const std::string& fontDir();        // data/fonts (desktop) or <root>/assets/fonts (device)
void setFontDir(std::string dir);
const std::string& selfExe();        // path of the running test executable (fake engine child)
void setSelfExe(std::string path);

} // namespace pchess::test

#define PCT_CONCAT2(a, b) a##b
#define PCT_CONCAT(a, b) PCT_CONCAT2(a, b)

#define TEST_CASE(name)                                                                                    \
    static void PCT_CONCAT(pct_test_, __LINE__)();                                                         \
    static ::pchess::test::Registrar PCT_CONCAT(pct_reg_, __LINE__)(name, __FILE__, __LINE__,             \
                                                                    &PCT_CONCAT(pct_test_, __LINE__));     \
    static void PCT_CONCAT(pct_test_, __LINE__)()

#define PCT_CHECK_IMPL(cond, text, isRequire)                                                              \
    do {                                                                                                   \
        ::pchess::test::countAssertion();                                                                  \
        if (!(cond)) {                                                                                     \
            ::pchess::test::reportFailure(__FILE__, __LINE__, text);                                       \
            if (isRequire) throw ::pchess::test::RequireFailed{};                                          \
        }                                                                                                  \
    } while (0)

#define PCT_CMP_IMPL(a, b, isRequire)                                                                      \
    do {                                                                                                   \
        ::pchess::test::countAssertion();                                                                  \
        const auto& pct_a = (a);                                                                           \
        const auto& pct_b = (b);                                                                           \
        if (!(pct_a == pct_b)) {                                                                           \
            ::pchess::test::reportFailure(__FILE__, __LINE__,                                              \
                                          std::string(#a " == " #b "  [") +                               \
                                              ::pchess::test::stringify(pct_a) + " vs " +                 \
                                              ::pchess::test::stringify(pct_b) + "]");                    \
            if (isRequire) throw ::pchess::test::RequireFailed{};                                          \
        }                                                                                                  \
    } while (0)

#define CHECK(cond) PCT_CHECK_IMPL(cond, "CHECK(" #cond ")", false)
#define CHECK_FALSE(cond) PCT_CHECK_IMPL(!(cond), "CHECK_FALSE(" #cond ")", false)
#define REQUIRE(cond) PCT_CHECK_IMPL(cond, "REQUIRE(" #cond ")", true)
#define REQUIRE_FALSE(cond) PCT_CHECK_IMPL(!(cond), "REQUIRE_FALSE(" #cond ")", true)
#define CHECK_EQ(a, b) PCT_CMP_IMPL(a, b, false)
#define REQUIRE_EQ(a, b) PCT_CMP_IMPL(a, b, true)
#define MESSAGE(text) ::pchess::test::message(::pchess::test::stringify(text))
