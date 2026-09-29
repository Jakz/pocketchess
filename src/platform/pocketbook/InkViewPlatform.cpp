// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/pocketbook/InkViewPlatform.h"

#include "app/Application.h"
#include "core/util/Log.h"
#include "core/util/Paths.h"

#include <inkview.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <gnu/libc-version.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>

// Older SDKs name keys KEY_*; 6.x uses IV_KEY_*.
#if !defined(IV_KEY_PREV) && defined(KEY_PREV)
#define IV_KEY_PREV KEY_PREV
#define IV_KEY_NEXT KEY_NEXT
#define IV_KEY_MENU KEY_MENU
#define IV_KEY_OK KEY_OK
#define IV_KEY_BACK KEY_BACK
#define IV_KEY_UP KEY_UP
#define IV_KEY_DOWN KEY_DOWN
#define IV_KEY_LEFT KEY_LEFT
#define IV_KEY_RIGHT KEY_RIGHT
#define IV_KEY_HOME KEY_HOME
#endif

namespace pchess::platform::pocketbook {

using util::Log;

namespace {

InkViewHost* g_host = nullptr;
std::unique_ptr<InkViewPresenter> g_presenter;
std::unique_ptr<app::Application> g_app;
app::AppConfig g_config;
bool g_started = false;

// Custom event id for PostMode::SendEvent (outside the documented EVT_* range).
constexpr int kPostEvent = 0x7E01;

int cHandler(int type, int par1, int par2) { return g_host ? g_host->handle(type, par1, par2) : 0; }

ui::KeyCode mapKey(int k) {
    using ui::KeyCode;
    switch (k) {
    case IV_KEY_PREV: return KeyCode::Prev;
    case IV_KEY_NEXT: return KeyCode::Next;
#ifdef IV_KEY_PREV2
    case IV_KEY_PREV2: return KeyCode::Prev;
#endif
#ifdef IV_KEY_NEXT2
    case IV_KEY_NEXT2: return KeyCode::Next;
#endif
    case IV_KEY_MENU: return KeyCode::Menu;
    case IV_KEY_OK: return KeyCode::Ok;
    case IV_KEY_BACK: return KeyCode::Back;
    case IV_KEY_UP: return KeyCode::Up;
    case IV_KEY_DOWN: return KeyCode::Down;
    case IV_KEY_LEFT: return KeyCode::Left;
    case IV_KEY_RIGHT: return KeyCode::Right;
    case IV_KEY_HOME: return KeyCode::Home;
    default: return KeyCode::None;
    }
}

std::filesystem::path exeDir() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = 0;
        return std::filesystem::path(buf).parent_path();
    }
    return "/mnt/ext1/applications";
}

} // namespace

// ------------------------------------------------------------------------------------------------
// InkViewHost

InkViewHost::InkViewHost() { g_host = this; }

void InkViewHost::runPosted() {
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard lock(postMutex_);
        batch.swap(posted_);
    }
    for (auto& fn : batch) fn();
    if (!batch.empty() && app_) app_->flush();
}

void InkViewHost::post(std::function<void()> fn) {
    {
        std::lock_guard lock(postMutex_);
        posted_.push_back(std::move(fn));
    }
    if (postMode_ == PostMode::SendEvent) SendEvent(cHandler, kPostEvent, 0, 0);
}

void InkViewHost::pollTrampoline() {
    if (!g_host) return;
    g_host->runPosted();
    if (g_host->backgroundRefs_ > 0) SetHardTimer("pchess-poll", &InkViewHost::pollTrampoline, 100);
}

void InkViewHost::setBackgroundActivity(bool active) {
    backgroundRefs_ = std::max(0, backgroundRefs_ + (active ? 1 : -1));
    if (postMode_ != PostMode::Poll) return;
    if (backgroundRefs_ > 0) SetHardTimer("pchess-poll", &InkViewHost::pollTrampoline, 100);
    else ClearTimer(&InkViewHost::pollTrampoline);
}

// All app timers share one InkView timer armed for the earliest deadline.
void InkViewHost::timerTrampoline() {
    if (g_host) g_host->fireTimers();
}

void InkViewHost::armTimer() {
    if (timers_.empty()) {
        ClearTimer(&InkViewHost::timerTrampoline);
        return;
    }
    auto earliest = std::min_element(timers_.begin(), timers_.end(),
                                     [](const auto& a, const auto& b) { return a.second.due < b.second.due; });
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(earliest->second.due - std::chrono::steady_clock::now()).count();
    SetHardTimer("pchess-timer", &InkViewHost::timerTrampoline, static_cast<int>(std::max<long long>(1, ms)));
}

void InkViewHost::fireTimers() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::function<void()>> due;
    for (auto it = timers_.begin(); it != timers_.end();) {
        if (it->second.due <= now) {
            due.push_back(std::move(it->second.fn));
            it = timers_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& fn : due) fn();
    armTimer();
    runPosted();
    if (app_) app_->flush();
}

TimerId InkViewHost::startTimer(std::chrono::milliseconds delay, std::function<void()> fn) {
    const TimerId id = nextTimer_++;
    timers_[id] = Timer{std::chrono::steady_clock::now() + delay, std::move(fn)};
    armTimer();
    return id;
}

void InkViewHost::cancelTimer(TimerId id) {
    timers_.erase(id);
    armTimer();
}

void InkViewHost::quit() { CloseApp(); }

int InkViewHost::batteryPercent() const { return GetBatteryPower(); }
bool InkViewHost::charging() const { return IsCharging() != 0; }

void InkViewHost::keyboardTrampoline(char* text) {
    if (!g_host) return;
    auto done = std::move(g_host->keyboardDone_);
    g_host->keyboardDone_ = nullptr;
    if (done) {
        if (text) done(std::string(text));
        else done(std::nullopt);
    }
    if (g_host->app_) {
        g_host->app_->invalidateAll(ui::RefreshHint::Screen);  // the keyboard covered the screen
        g_host->app_->flush();
    }
}

void InkViewHost::request(const std::string& title, const std::string& initial,
                          std::function<void(std::optional<std::string>)> done) {
    static char buffer[1024];
    std::snprintf(buffer, sizeof buffer, "%s", initial.c_str());
    keyboardDone_ = std::move(done);
    OpenKeyboard(title.c_str(), buffer, static_cast<int>(sizeof buffer) - 1, 0, &InkViewHost::keyboardTrampoline);
}

InfoList InkViewHost::systemInfo() const {
    InfoList info;
    const char* model = GetDeviceModel();
    const char* fw = GetSoftwareVersion();
    info.emplace_back("Device", std::string(model ? model : "?") + ", firmware " + (fw ? fw : "?"));
    info.emplace_back("glibc", gnu_get_libc_version());
    struct sysinfo si {};
    if (::sysinfo(&si) == 0) {
        const unsigned long long unit = si.mem_unit ? si.mem_unit : 1;
        info.emplace_back("RAM", std::to_string(si.totalram * unit / (1024 * 1024)) + " MiB total, " +
                                     std::to_string(si.freeram * unit / (1024 * 1024)) + " MiB free");
    }
    info.emplace_back("CPUs online", std::to_string(::sysconf(_SC_NPROCESSORS_ONLN)));
    if (app_) {
        const std::string root = app_->paths().root().string();
        struct statvfs vfs {};
        std::string mount = "?";
        if (::statvfs(root.c_str(), &vfs) == 0)
            mount = std::string((vfs.f_flag & ST_NOEXEC) ? "noexec" : "exec ok") + ", " +
                    std::to_string(static_cast<unsigned long long>(vfs.f_bavail) * vfs.f_frsize / (1024 * 1024)) + " MiB free";
        info.emplace_back("Data root", std::string(::access(root.c_str(), W_OK) == 0 ? "writable" : "NOT writable") + ", " + mount);
    }
    info.emplace_back("post()", postMode_ == PostMode::Poll ? "poll 100 ms while active" : "SendEvent");
    return info;
}

int InkViewHost::handle(int type, int par1, int par2) {
    using T = ui::InputEvent::Type;
    if (type == kPostEvent) {
        runPosted();
        return 1;
    }
    if (!app_) return 0;
    switch (type) {
    case EVT_INIT: return 1;
    case EVT_SHOW:
        if (!g_started) {
            g_started = true;
            app_->start();
        } else {
            app_->handleLifecycle(Lifecycle::Show);
        }
        return 1;
    case EVT_EXIT:
        app_->handleLifecycle(Lifecycle::Exit);
        util::Log::info("app", "exit");
        util::Log::closeFile();
        return 1;
#ifdef EVT_FOREGROUND
    case EVT_FOREGROUND: app_->handleLifecycle(Lifecycle::Foreground); return 1;
#endif
#ifdef EVT_BACKGROUND
    case EVT_BACKGROUND: app_->handleLifecycle(Lifecycle::Background); return 1;
#endif
    case EVT_POINTERDOWN: app_->handleInput(ui::InputEvent::pointer(T::PointerDown, {par1, par2})); return 1;
    case EVT_POINTERUP: app_->handleInput(ui::InputEvent::pointer(T::PointerUp, {par1, par2})); return 1;
    case EVT_POINTERMOVE: app_->handleInput(ui::InputEvent::pointer(T::PointerMove, {par1, par2})); return 1;
    case EVT_POINTERLONG: app_->handleInput(ui::InputEvent::pointer(T::LongPress, {par1, par2})); return 1;
    case EVT_KEYPRESS:
    case EVT_KEYREPEAT: {
        const ui::KeyCode k = mapKey(par1);
        ui::InputEvent e = ui::InputEvent::keyPress(k, type == EVT_KEYREPEAT, par1);
        app_->handleInput(e);
        return k == ui::KeyCode::None ? 0 : 1;  // unknown keys go to the system
    }
    case EVT_KEYRELEASE: return mapKey(par1) == ui::KeyCode::None ? 0 : 1;
#ifdef EVT_MTSYNC
    case EVT_MTSYNC: return 0;  // multitouch frame marker sent with every touch (FW 6.10); single touch only
#endif
    default:
        util::Log::info("inkview", "event " + std::to_string(type) + " (" + std::to_string(par1) + ", " + std::to_string(par2) + ")");
        return 0;
    }
}

// ------------------------------------------------------------------------------------------------
// InkViewPresenter

InkViewPresenter::InkViewPresenter() {
    profile_ = DeviceProfile::verseProColor();
    profile_.width = ScreenWidth();
    profile_.height = ScreenHeight();
    profile_.name = "device";
}

InfoList InkViewPresenter::info() const {
    InfoList info;
    if (icanvas* c = GetCanvas()) {
        info.emplace_back("InkView canvas", std::to_string(c->width) + "x" + std::to_string(c->height) + ", depth " +
                                                std::to_string(c->depth) + ", scanline " + std::to_string(c->scanline));
    }
    info.emplace_back("Update calls", "fast/partial: PartialUpdate, full: FullUpdate (blit: Stretch IMAGE_RGB)");
    char ms[32];
    std::snprintf(ms, sizeof ms, "%.1f ms", lastMs_);
    info.emplace_back("Last update", ms);
    return info;
}

void InkViewPresenter::present(const ui::Canvas& canvas, const ui::PresentRequest& req) {
    const auto t0 = std::chrono::steady_clock::now();
    for (const ui::Rect& r : req.rects) {
        const uint8_t* src = canvas.row(r.y) + r.x * 3;
        // Stretch converts RGB to the panel format; 1:1 size = plain copy (Enigmistica pattern).
        Stretch(src, IMAGE_RGB, r.w, r.h, canvas.stride(), r.x, r.y, r.w, r.h, 0);
    }
    if (req.mode == ui::RefreshMode::Full) {
        FullUpdate();
    } else {
        for (const ui::Rect& r : req.rects) PartialUpdate(r.x, r.y, r.w, r.h);
    }
    lastMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ------------------------------------------------------------------------------------------------

int runInkViewApp(int argc, char** argv) {
    std::optional<std::filesystem::path> cliRoot;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--root") == 0) cliRoot = argv[i + 1];
    g_config.root = util::Paths::resolveRoot(cliRoot, exeDir());
    util::Log::setSink([](util::LogLevel, std::string_view line) {
        std::fprintf(stderr, "%.*s\n", static_cast<int>(line.size()), line.data());
    });

    static InkViewHost host;
    struct Init {
        static int handler(int type, int p1, int p2) {
            if (type == EVT_INIT && !g_app) {
                g_presenter = std::make_unique<InkViewPresenter>();
                g_app = std::make_unique<app::Application>(*g_host, *g_presenter, g_config);
                g_host->attach(g_app.get());
            }
            return cHandler(type, p1, p2);
        }
    };
    InkViewMain(&Init::handler);
    return 0;
}

} // namespace pchess::platform::pocketbook
