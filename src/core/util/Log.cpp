// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace pchess::util {
namespace {

struct LogState {
    std::mutex mutex;
    std::FILE* file = nullptr;
    std::filesystem::path path;
    std::uintmax_t rotateBytes = 1024 * 1024;
    Log::Sink sink;
    LogLevel level = LogLevel::Info;
    bool timing = false;
};

LogState& state() {
    static LogState s;
    return s;
}

const char* levelName(LogLevel l) {
    switch (l) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warn: return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}

void rotateIfNeeded(LogState& s) {
    if (!s.file) return;
    long pos = std::ftell(s.file);
    if (pos < 0 || static_cast<std::uintmax_t>(pos) < s.rotateBytes) return;
    std::fclose(s.file);
    std::error_code ec;
    std::filesystem::path old = s.path;
    old += ".1";
    std::filesystem::remove(old, ec);
    std::filesystem::rename(s.path, old, ec);
    s.file = std::fopen(s.path.string().c_str(), "ab");
}

} // namespace

void Log::openFile(const std::filesystem::path& file, std::uintmax_t rotateBytes) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file) std::fclose(s.file);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    s.path = file;
    s.rotateBytes = rotateBytes;
    s.file = std::fopen(file.string().c_str(), "ab");
}

void Log::closeFile() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file) std::fclose(s.file);
    s.file = nullptr;
}

void Log::setSink(Sink sink) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.sink = std::move(sink);
}

void Log::setLevel(LogLevel level) { state().level = level; }
void Log::setTimingEnabled(bool enabled) { state().timing = enabled; }
bool Log::timingEnabled() { return state().timing; }

void Log::write(LogLevel level, std::string_view tag, std::string_view message) {
    auto& s = state();
    if (level < s.level) return;

    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);

    std::string line;
    line.reserve(message.size() + tag.size() + 40);
    line += stamp;
    line += ' ';
    line += levelName(level);
    line += " [";
    line += tag;
    line += "] ";
    line += message;

    std::lock_guard lock(s.mutex);
    if (s.file) {
        std::fwrite(line.data(), 1, line.size(), s.file);
        std::fputc('\n', s.file);
        std::fflush(s.file);
        rotateIfNeeded(s);
    }
    if (s.sink) s.sink(level, line);
}

} // namespace pchess::util
