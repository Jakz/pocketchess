// SPDX-License-Identifier: GPL-3.0-or-later
// Windows child process: CreateProcessW + anonymous pipes + a kill-on-close job object, so the
// engine always dies with the app (architecture doc §2).
#include "core/engine/Process.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <mutex>
#include <thread>

#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")
#endif

namespace pchess::engine {
namespace {

std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += c;
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            out += c;
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string lastErrorText() {
    const DWORD e = GetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, buf, sizeof buf, nullptr);
    return std::string(buf) + " (" + std::to_string(e) + ")";
}

class Win32Process final : public Process {
public:
    Win32Process(PROCESS_INFORMATION pi, HANDLE job, HANDLE inWrite, HANDLE outRead, LineCallback onLine, ExitCallback onExit)
        : pi_(pi), job_(job), in_(inWrite), out_(outRead), onLine_(std::move(onLine)), onExit_(std::move(onExit)) {
        reader_ = std::thread([this] { readLoop(); });
    }

    ~Win32Process() override {
        terminate(std::chrono::milliseconds(300));
        if (job_) CloseHandle(job_);
        CloseHandle(pi_.hThread);
        CloseHandle(pi_.hProcess);
    }

    void writeLine(std::string_view line) override {
        std::lock_guard lock(writeMutex_);
        if (!in_) return;
        std::string s(line);
        s += '\n';
        DWORD written = 0;
        WriteFile(in_, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
    }

    void terminate(std::chrono::milliseconds grace) override {
        {
            std::lock_guard lock(writeMutex_);
            if (in_) {
                CloseHandle(in_);
                in_ = nullptr;
            }
        }
        if (WaitForSingleObject(pi_.hProcess, static_cast<DWORD>(grace.count())) == WAIT_TIMEOUT)
            TerminateProcess(pi_.hProcess, 1);
        if (reader_.joinable()) reader_.join();
        if (out_) {
            CloseHandle(out_);
            out_ = nullptr;
        }
    }

    void setLowPriority(int nice) override {
        const DWORD cls = nice <= 0 ? NORMAL_PRIORITY_CLASS : (nice >= 15 ? IDLE_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS);
        SetPriorityClass(pi_.hProcess, cls);
    }

    ProcessStats stats() const override {
        ProcessStats s;
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(pi_.hProcess, &created, &exited, &kernel, &user)) {
            auto toU64 = [](FILETIME f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            s.cpuSeconds = static_cast<double>(toU64(kernel) + toU64(user)) / 1e7;
            s.valid = true;
        }
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(pi_.hProcess, &pmc, sizeof pmc)) {
            s.rssKb = pmc.WorkingSetSize / 1024;
            s.peakRssKb = pmc.PeakWorkingSetSize / 1024;
        }
        return s;
    }

    bool running() const override { return !exited_.load(); }
    int pid() const override { return static_cast<int>(pi_.dwProcessId); }

private:
    void readLoop() {
        std::string buf;
        char chunk[4096];
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(out_, chunk, sizeof chunk, &n, nullptr) || n == 0) break;
            buf.append(chunk, n);
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string_view line(buf.data(), pos);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                if (onLine_) onLine_(line);
                buf.erase(0, pos + 1);
            }
        }
        if (!buf.empty() && onLine_) onLine_(buf);
        WaitForSingleObject(pi_.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi_.hProcess, &code);
        exited_.store(true);
        if (onExit_) onExit_(static_cast<int>(code));
    }

    PROCESS_INFORMATION pi_;
    HANDLE job_;
    HANDLE in_;
    HANDLE out_;
    LineCallback onLine_;
    ExitCallback onExit_;
    std::thread reader_;
    std::mutex writeMutex_;
    std::atomic<bool> exited_{false};
};

} // namespace

std::unique_ptr<Process> Process::spawn(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                        LineCallback onLine, ExitCallback onExit, std::string* error) {
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0)) {
        if (error) *error = lastErrorText();
        return nullptr;
    }
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = quoteArg(exe.wstring());
    for (const auto& a : args) cmd += L" " + quoteArg(widen(a));
    const std::wstring dir = exe.has_parent_path() ? exe.parent_path().wstring() : std::wstring();

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (!ok) {
        if (error) *error = lastErrorText();
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return nullptr;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info);
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(pi.hThread);
    return std::make_unique<Win32Process>(pi, job, inWrite, outRead, std::move(onLine), std::move(onExit));
}

} // namespace pchess::engine
