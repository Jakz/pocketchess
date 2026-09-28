// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Clock.h"

#include "core/util/Log.h"

namespace pchess::util {

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
