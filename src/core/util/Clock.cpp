// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Clock.h"

#include "core/util/Log.h"

namespace pchess::util {

CivilTime civilFromUnix(int64_t t) {
    int64_t days = t / 86400, secs = t % 86400;
    if (secs < 0) {
        secs += 86400;
        --days;
    }
    // civil_from_days (H. Hinnant): days since 1970-01-01 -> y/m/d.
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    CivilTime c;
    c.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    c.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    c.year = static_cast<int>(yoe + era * 400 + (c.month <= 2 ? 1 : 0));
    c.hour = static_cast<int>(secs / 3600);
    c.minute = static_cast<int>(secs / 60 % 60);
    c.second = static_cast<int>(secs % 60);
    return c;
}

int64_t SystemClock::unixSeconds() const {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

SystemClock& SystemClock::instance() {
    static SystemClock clock;
    return clock;
}

ScopedTimer::ScopedTimer(const char* label) : label_(label), start_(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    if (Log::timingEnabled())
        Log::info("timing", std::string(label_) + " " + std::to_string(elapsedMs()) + " ms");
}

double ScopedTimer::elapsedMs() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
}

} // namespace pchess::util
