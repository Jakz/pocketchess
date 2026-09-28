// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace pchess::util {

enum class LogLevel { Debug, Info, Warn, Error };

// Process-wide logger: logs/pocketchess.log (rotated at 1 MB) plus an optional extra sink
// (OutputDebugString on Windows, stderr on the device).
class Log {
public:
    using Sink = std::function<void(LogLevel, std::string_view line)>;

    static void openFile(const std::filesystem::path& file, std::uintmax_t rotateBytes = 1024 * 1024);
    static void closeFile();
    static void setSink(Sink sink);
    static void setLevel(LogLevel level);
    static void setTimingEnabled(bool enabled);
    static bool timingEnabled();

    static void write(LogLevel level, std::string_view tag, std::string_view message);
    static void debug(std::string_view tag, std::string_view m) { write(LogLevel::Debug, tag, m); }
    static void info(std::string_view tag, std::string_view m) { write(LogLevel::Info, tag, m); }
    static void warn(std::string_view tag, std::string_view m) { write(LogLevel::Warn, tag, m); }
    static void error(std::string_view tag, std::string_view m) { write(LogLevel::Error, tag, m); }
};

} // namespace pchess::util
