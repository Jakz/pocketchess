// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace pchess::util {

// UI-thread event-loop services for code below the platform layer (engine subsystem). The app
// implements it over platform::Host; tests use a manual scheduler with fake time.
class Scheduler {
public:
    using TimerId = uint64_t;
    virtual ~Scheduler() = default;
    // Thread-safe: runs `fn` on the UI thread.
    virtual void post(std::function<void()> fn) = 0;
    // One-shot timer on the UI thread. Ids are never 0.
    virtual TimerId startTimer(std::chrono::milliseconds delay, std::function<void()> fn) = 0;
    virtual void cancelTimer(TimerId id) = 0;
    virtual std::chrono::steady_clock::time_point now() const = 0;
    // A background thread may post() while this is on (hosts that poll, ADR 0003). Counted.
    virtual void setBackgroundActivity(bool active) { (void)active; }
};

// RAII one-shot timer handle: cancelled on destruction or restart.
class Timer {
public:
    Timer() = default;
    explicit Timer(Scheduler* s) : s_(s) {}
    ~Timer() { cancel(); }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    void bind(Scheduler* s) {
        cancel();
        s_ = s;
    }
    void start(std::chrono::milliseconds delay, std::function<void()> fn) {
        cancel();
        if (!s_) return;
        id_ = s_->startTimer(delay, [this, fn = std::move(fn)] {
            id_ = 0;
            fn();
        });
    }
    void cancel() {
        if (s_ && id_) s_->cancelTimer(id_);
        id_ = 0;
    }
    bool active() const { return id_ != 0; }

private:
    Scheduler* s_ = nullptr;
    Scheduler::TimerId id_ = 0;
};

} // namespace pchess::util
