// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

namespace pchess::util {

// Every time source goes through Clock so tests can fake it (testing doc §1).
class Clock {
public:
    virtual ~Clock() = default;
    // Wall-clock time, seconds since the Unix epoch (review scheduling, logs).
    virtual int64_t unixSeconds() const = 0;
    // Monotonic time for durations and timeouts.
    virtual std::chrono::steady_clock::time_point now() const = 0;
    // Local time minus UTC at `unixSeconds` (dates shown to the user). Fakes stay on UTC.
    virtual int64_t utcOffsetSeconds(int64_t) const { return 0; }
};

// Calendar fields (UTC, proleptic Gregorian) of unix seconds: PGN dates, file names, reports.
struct CivilTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
};
CivilTime civilFromUnix(int64_t unixSeconds);

class SystemClock final : public Clock {
public:
    int64_t unixSeconds() const override;
    std::chrono::steady_clock::time_point now() const override { return std::chrono::steady_clock::now(); }
    int64_t utcOffsetSeconds(int64_t unixSeconds) const override;
    static SystemClock& instance();
};

class FakeClock final : public Clock {
public:
    explicit FakeClock(int64_t unixSeconds = 1'790'000'000) : unix_(unixSeconds) {}
    int64_t unixSeconds() const override { return unix_; }
    std::chrono::steady_clock::time_point now() const override { return steady_; }
    void advance(std::chrono::milliseconds ms) {
        steady_ += ms;
        unix_ += ms.count() / 1000;
    }
    void setUnixSeconds(int64_t s) { unix_ = s; }

private:
    int64_t unix_;
    std::chrono::steady_clock::time_point steady_{};
};

// Measures a scope and logs it at info level when timing logs are enabled (testing doc §4).
class ScopedTimer {
public:
    explicit ScopedTimer(const char* label);
    ~ScopedTimer();
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    double elapsedMs() const;

private:
    const char* label_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace pchess::util
