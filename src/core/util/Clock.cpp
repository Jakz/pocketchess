// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Clock.h"

#include "core/util/Log.h"

#include <ctime>

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

int64_t SystemClock::utcOffsetSeconds(int64_t unixSeconds) const {
    // Standard C only: the broken-down local and UTC times of the same instant, read back as if
    // both were UTC; their difference is the offset (DST included). std::localtime/gmtime share a
    // static buffer: copy each result before the next call (one UI thread uses this).
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm local{}, utc{};
    if (const std::tm* l = std::localtime(&t)) local = *l;
    else return 0;
    if (const std::tm* u = std::gmtime(&t)) utc = *u;
    else return 0;
    auto days = [](const std::tm& x) {  // days since 1970-01-01 of a broken-down date
        const int y = x.tm_year + 1900 - (x.tm_mon < 2), m = (x.tm_mon + 9) % 12;
        const int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
        const int doy = (153 * m + 2) / 5 + x.tm_mday - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return int64_t(era) * 146097 + doe - 719468;
    };
    auto secs = [&](const std::tm& x) { return days(x) * 86400 + x.tm_hour * 3600 + x.tm_min * 60 + x.tm_sec; };
    return secs(local) - secs(utc);
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
