// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pchess::engine {

struct ProcessStats {
    bool valid = false;
    double cpuSeconds = 0;  // user + system time of the child
    uint64_t rssKb = 0;     // current resident set
    uint64_t peakRssKb = 0; // peak resident set (where the OS reports it)
};

// Child process with line-based stdio (engines over UCI). Pure interface: the implementation
// is chosen at link time (platform/win32/Win32Process.cpp or platform/posix/PosixProcess.cpp),
// so core never includes OS headers. Lives in core (not platform/) because the engine layer is
// written against it (architecture doc §1: "UCI over an abstract Process").
class Process {
public:
    // Called on the reader thread: marshal to the UI thread (Host::post) before touching state.
    using LineCallback = std::function<void(std::string_view line)>;
    using ExitCallback = std::function<void(int exitCode)>;

    static std::unique_ptr<Process> spawn(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                          LineCallback onLine, ExitCallback onExit, std::string* error = nullptr);

    virtual ~Process() = default;  // terminates (short grace) and reaps
    virtual void writeLine(std::string_view line) = 0;
    // Closes stdin, waits up to `grace` for a voluntary exit, then kills.
    virtual void terminate(std::chrono::milliseconds grace) = 0;
    // 0 = normal; higher = lower priority (nice level on POSIX, BELOW_NORMAL/IDLE on Windows).
    virtual void setLowPriority(int niceLevel) = 0;
    virtual ProcessStats stats() const = 0;
    virtual bool running() const = 0;
    virtual int pid() const = 0;
};

} // namespace pchess::engine
