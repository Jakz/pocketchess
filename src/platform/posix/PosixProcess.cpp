// SPDX-License-Identifier: GPL-3.0-or-later
// POSIX child process (device and desktop Linux): pipe/fork/execv, a reader thread, nice,
// SIGKILL after a grace period, reaped with wait4 so CPU time and peak RSS survive the exit.
#include "core/engine/Process.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace pchess::engine {
namespace {

class PosixProcess final : public Process {
public:
    PosixProcess(pid_t pid, int inFd, int outFd, LineCallback onLine, ExitCallback onExit)
        : pid_(pid), in_(inFd), out_(outFd), onLine_(std::move(onLine)), onExit_(std::move(onExit)) {
        reader_ = std::thread([this] { readLoop(); });
    }

    ~PosixProcess() override { terminate(std::chrono::milliseconds(300)); }

    void writeLine(std::string_view line) override {
        std::lock_guard lock(writeMutex_);
        if (in_ < 0) return;
        std::string s(line);
        s += '\n';
        const char* p = s.data();
        size_t left = s.size();
        while (left > 0) {
            const ssize_t n = ::write(in_, p, left);
            if (n < 0) {
                if (errno == EINTR) continue;
                return;  // child gone (EPIPE); the exit callback reports it
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
    }

    void terminate(std::chrono::milliseconds grace) override {
        {
            std::lock_guard lock(writeMutex_);
            if (in_ >= 0) {
                ::close(in_);  // EOF on stdin makes UCI engines quit
                in_ = -1;
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + grace;
        while (!exited_.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!exited_.load()) ::kill(pid_, SIGKILL);
        if (reader_.joinable()) reader_.join();
        if (out_ >= 0) {
            ::close(out_);
            out_ = -1;
        }
    }

    void setLowPriority(int nice) override { ::setpriority(PRIO_PROCESS, static_cast<id_t>(pid_), nice); }

    ProcessStats stats() const override {
        if (exited_.load()) {
            std::lock_guard lock(statsMutex_);
            return final_;
        }
        ProcessStats s;
        const std::string base = "/proc/" + std::to_string(pid_);
        std::ifstream stat(base + "/stat");
        std::string content((std::istreambuf_iterator<char>(stat)), std::istreambuf_iterator<char>());
        const size_t rp = content.rfind(')');
        if (rp != std::string::npos) {
            std::istringstream is(content.substr(rp + 2));
            std::string field;
            unsigned long long utime = 0, stime = 0;
            for (int i = 3; i <= 15 && (is >> field); ++i) {
                if (i == 14) utime = std::strtoull(field.c_str(), nullptr, 10);
                if (i == 15) stime = std::strtoull(field.c_str(), nullptr, 10);
            }
            const long tck = ::sysconf(_SC_CLK_TCK);
            if (tck > 0) s.cpuSeconds = static_cast<double>(utime + stime) / static_cast<double>(tck);
            s.valid = true;
        }
        std::ifstream status(base + "/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("VmRSS:", 0) == 0) s.rssKb = std::strtoull(line.c_str() + 6, nullptr, 10);
            else if (line.rfind("VmHWM:", 0) == 0) s.peakRssKb = std::strtoull(line.c_str() + 6, nullptr, 10);
        }
        return s;
    }

    bool running() const override { return !exited_.load(); }
    int pid() const override { return static_cast<int>(pid_); }

private:
    void readLoop() {
        std::string buf;
        char chunk[4096];
        for (;;) {
            const ssize_t n = ::read(out_, chunk, sizeof chunk);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            buf.append(chunk, static_cast<size_t>(n));
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string_view line(buf.data(), pos);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                if (onLine_) onLine_(line);
                buf.erase(0, pos + 1);
            }
        }
        if (!buf.empty() && onLine_) onLine_(buf);
        int status = 0;
        struct rusage ru {};
        pid_t r;
        do {
            r = ::wait4(pid_, &status, 0, &ru);
        } while (r < 0 && errno == EINTR);
        {
            std::lock_guard lock(statsMutex_);
            final_.valid = r == pid_;
            final_.cpuSeconds = static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
                                static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
            final_.peakRssKb = static_cast<uint64_t>(ru.ru_maxrss);  // KiB on Linux
        }
        exited_.store(true);
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        if (onExit_) onExit_(code);
    }

    pid_t pid_;
    int in_, out_;
    LineCallback onLine_;
    ExitCallback onExit_;
    std::thread reader_;
    std::mutex writeMutex_;
    mutable std::mutex statsMutex_;
    ProcessStats final_;
    std::atomic<bool> exited_{false};
};

} // namespace

std::unique_ptr<Process> Process::spawn(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                        LineCallback onLine, ExitCallback onExit, std::string* error) {
    // Writing to a dead engine must not kill the app.
    std::signal(SIGPIPE, SIG_IGN);

    int toChild[2], fromChild[2];
    if (::pipe2(toChild, O_CLOEXEC) != 0) {
        if (error) *error = std::strerror(errno);
        return nullptr;
    }
    if (::pipe2(fromChild, O_CLOEXEC) != 0) {
        if (error) *error = std::strerror(errno);
        ::close(toChild[0]);
        ::close(toChild[1]);
        return nullptr;
    }

    const std::string exeStr = exe.string();
    const std::string dir = exe.has_parent_path() ? exe.parent_path().string() : std::string(".");
    std::vector<std::string> argStore;
    argStore.push_back(exeStr);
    for (const auto& a : args) argStore.push_back(a);
    std::vector<char*> argv;
    for (auto& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        if (error) *error = std::strerror(errno);
        for (int fd : {toChild[0], toChild[1], fromChild[0], fromChild[1]}) ::close(fd);
        return nullptr;
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls from here on.
        ::dup2(toChild[0], STDIN_FILENO);
        ::dup2(fromChild[1], STDOUT_FILENO);
        ::dup2(fromChild[1], STDERR_FILENO);
        if (::chdir(dir.c_str()) != 0) { /* keep the parent's cwd */ }
        ::execv(exeStr.c_str(), argv.data());
        const char msg[] = "exec failed\n";
        ssize_t ignored = ::write(STDOUT_FILENO, msg, sizeof msg - 1);
        (void)ignored;
        ::_exit(127);
    }
    ::close(toChild[0]);
    ::close(fromChild[1]);
    return std::make_unique<PosixProcess>(pid, toChild[1], fromChild[0], std::move(onLine), std::move(onExit));
}

} // namespace pchess::engine
