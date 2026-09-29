// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/HeadlessHost.h"

namespace pchess::app {

void HeadlessHost::post(std::function<void()> fn) {
    std::lock_guard lock(mutex_);
    posted_.push_back(std::move(fn));
}

platform::TimerId HeadlessHost::startTimer(std::chrono::milliseconds delay, std::function<void()> fn) {
    const platform::TimerId id = nextId_++;
    timers_[id] = Timer{now_ + delay, std::move(fn)};
    return id;
}

void HeadlessHost::cancelTimer(platform::TimerId id) { timers_.erase(id); }

void HeadlessHost::runPosted() {
    // Until idle: posted functions may post more (engine replies, job results).
    for (int round = 0; round < 1000; ++round) {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard lock(mutex_);
            batch.swap(posted_);
        }
        if (batch.empty()) return;
        for (auto& fn : batch) fn();
    }
}

void HeadlessHost::advance(std::chrono::milliseconds ms) {
    now_ += ms;
    runPosted();
    for (;;) {
        auto due = timers_.end();
        for (auto it = timers_.begin(); it != timers_.end(); ++it)
            if (it->second.due <= now_ && (due == timers_.end() || it->second.due < due->second.due)) due = it;
        if (due == timers_.end()) break;
        auto fn = std::move(due->second.fn);
        timers_.erase(due);
        fn();
        runPosted();
    }
}

} // namespace pchess::app
