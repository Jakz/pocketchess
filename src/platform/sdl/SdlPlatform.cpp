// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sdl/SdlPlatform.h"

#include "app/Application.h"
#include "core/util/Log.h"
#include "ui/canvas/PngWriter.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <thread>

namespace pchess::platform::sdl {

using util::Log;

namespace {
// Event type used by SDL's timer thread; set once in SdlHost::init.
std::atomic<Uint32> g_timerEvent{0};
} // namespace

// ------------------------------------------------------------------------------------------------
// SdlHost

SdlHost::SdlHost() = default;

SdlHost::~SdlHost() {
    for (auto& [id, t] : timers_) SDL_RemoveTimer(t.sdl);
    presenter_.reset();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool SdlHost::init(const DeviceProfile& profile, double scale, std::string& error) {
    profile_ = profile;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        error = SDL_GetError();
        return false;
    }
    if (scale <= 0) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.h > 0)
            scale = std::min(1.0, (usable.h - 60.0) / profile.height);
        else
            scale = 0.6;
    }
    scale_ = std::clamp(scale, 0.2, 2.0);
    window_ = SDL_CreateWindow("PocketChess", static_cast<int>(profile.width * scale_), static_cast<int>(profile.height * scale_),
                               SDL_WINDOW_RESIZABLE);
    if (!window_) {
        error = SDL_GetError();
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        error = SDL_GetError();
        return false;
    }
    postEvent_ = SDL_RegisterEvents(2);
    if (postEvent_ == 0) {
        error = "SDL_RegisterEvents failed";
        return false;
    }
    timerEvent_ = postEvent_ + 1;
    g_timerEvent = timerEvent_;
    SDL_StartTextInput(window_);
    presenter_ = std::make_unique<SdlPresenter>(window_, renderer_, profile, *this);
    return true;
}

void SdlHost::setScale(double scale) {
    scale_ = std::clamp(scale, 0.2, 2.0);
    SDL_SetWindowSize(window_, static_cast<int>(profile_.width * scale_), static_cast<int>(profile_.height * scale_));
    presenter_->repaint();
}

ui::Point SdlHost::toCanvas(float x, float y) const {
    int w = 1, h = 1;
    SDL_GetWindowSize(window_, &w, &h);
    const float fx = x * static_cast<float>(profile_.width) / static_cast<float>(std::max(1, w));
    const float fy = y * static_cast<float>(profile_.height) / static_cast<float>(std::max(1, h));
    return {static_cast<int>(fx), static_cast<int>(fy)};
}

void SdlHost::post(std::function<void()> fn) {
    {
        std::lock_guard lock(postMutex_);
        posted_.push_back(std::move(fn));
    }
    SDL_Event e{};
    e.type = postEvent_;
    SDL_PushEvent(&e);
}

Uint32 SDLCALL SdlHost::timerCallback(void* userdata, SDL_TimerID, Uint32) {
    // Runs on SDL's timer thread: only push an event; the UI thread runs the callback.
    SDL_Event e{};
    e.type = g_timerEvent.load();
    e.user.data1 = userdata;  // our TimerId, smuggled as a pointer-sized integer
    SDL_PushEvent(&e);
    return 0;  // one-shot
}

TimerId SdlHost::startTimer(std::chrono::milliseconds delay, std::function<void()> fn) {
    const TimerId id = nextTimer_++;
    Timer t;
    t.fn = std::move(fn);
    t.sdl = SDL_AddTimer(static_cast<Uint32>(std::max<int64_t>(1, delay.count())), &SdlHost::timerCallback,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
    timers_[id] = std::move(t);
    return id;
}

void SdlHost::cancelTimer(TimerId id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    SDL_RemoveTimer(it->second.sdl);
    timers_.erase(it);
}

void SdlHost::quit() {
    running_ = false;
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}

InfoList SdlHost::systemInfo() const {
    InfoList info;
    info.emplace_back("SDL", std::to_string(SDL_MAJOR_VERSION) + "." + std::to_string(SDL_MINOR_VERSION) + "." +
                                 std::to_string(SDL_MICRO_VERSION));
    info.emplace_back("CPU cores", std::to_string(SDL_GetNumLogicalCPUCores()));
    info.emplace_back("System RAM", std::to_string(SDL_GetSystemRAM()) + " MiB");
    info.emplace_back("Keys", "PgUp/PgDn/arrows=Prev/Next, M=Menu, Esc=Back, Enter=OK; F2 display, F3 rects, "
                              "F4 flash, F6 latency, F9 sleep/wake, F5 battery, F12 screenshot, Ctrl+wheel zoom");
    return info;
}

std::string SdlHost::platformName() const {
    const std::string p = SDL_GetPlatform();
    if (p == "Windows") return "windows";
    if (p == "Linux") return "linux";
    if (p == "macOS") return "macos";
    return p;
}

void SdlHost::handleKey(app::Application& app, const SDL_KeyboardEvent& k) {
    using ui::KeyCode;
    const SDL_Keycode key = k.key;
    const int raw = static_cast<int>(key);

    // Simulator controls (debug only, never in the device build).
    switch (key) {
    case SDLK_F2:
        presenter_->cycleDisplayMode(app.canvas());
        Log::info("sim", std::string("display mode: ") + EinkSimulator::modeName(presenter_->simulator().mode()));
        return;
    case SDLK_F3: presenter_->toggleOverlay(); return;
    case SDLK_F4: presenter_->toggleFlash(); return;
    case SDLK_F6: presenter_->toggleLatency(); presenter_->updateTitle(); return;
    case SDLK_F5:
        battery_ = battery_ <= 5 ? 100 : battery_ - 10;
        Log::info("sim", "battery " + std::to_string(battery_) + "%");
        return;
    case SDLK_F9:
        app.handleLifecycle(Lifecycle::Sleep);
        app.handleLifecycle(Lifecycle::Wake);
        return;
    case SDLK_F12: {
        const std::time_t t = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof name, "screenshot-%Y%m%d-%H%M%S.png", std::localtime(&t));
        const auto file = app.paths().logs() / name;
        if (ui::writePng(file, app.canvas())) Log::info("sim", "saved " + file.string());
        return;
    }
    default: break;
    }

    if (app.wantsText()) {
        // Text entry: characters arrive as SDL_EVENT_TEXT_INPUT.
        if (key == SDLK_BACKSPACE) app.handleInput(ui::InputEvent::typed("\b"));
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) app.handleInput(ui::InputEvent::keyPress(KeyCode::Ok, k.repeat, raw));
        else if (key == SDLK_ESCAPE) app.handleInput(ui::InputEvent::keyPress(KeyCode::Back, k.repeat, raw));
        return;
    }

    KeyCode code = KeyCode::None;
    switch (key) {
    case SDLK_PAGEUP: case SDLK_LEFT: code = KeyCode::Prev; break;
    case SDLK_PAGEDOWN: case SDLK_RIGHT: code = KeyCode::Next; break;
    case SDLK_UP: code = KeyCode::Up; break;
    case SDLK_DOWN: code = KeyCode::Down; break;
    case SDLK_M: case SDLK_F10: code = KeyCode::Menu; break;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: code = KeyCode::Back; break;
    case SDLK_HOME: code = KeyCode::Home; break;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: code = KeyCode::Ok; break;
    default: break;
    }
    if (code != KeyCode::None) app.handleInput(ui::InputEvent::keyPress(code, k.repeat, raw));
}

void SdlHost::handleEvent(app::Application& app, const SDL_Event& e) {
    using T = ui::InputEvent::Type;
    if (e.type == postEvent_) {
        std::deque<std::function<void()>> batch;
        {
            std::lock_guard lock(postMutex_);
            batch.swap(posted_);
        }
        for (auto& fn : batch) fn();
        app.flush();
        return;
    }
    if (e.type == timerEvent_) {
        const TimerId id = static_cast<TimerId>(reinterpret_cast<uintptr_t>(e.user.data1));
        auto it = timers_.find(id);
        if (it == timers_.end()) return;
        auto fn = std::move(it->second.fn);
        timers_.erase(it);
        fn();
        app.flush();
        return;
    }
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        app.handleLifecycle(Lifecycle::Exit);
        running_ = false;
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        presenter_->repaint();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: break;
    case SDL_EVENT_KEY_DOWN: handleKey(app, e.key); break;
    case SDL_EVENT_TEXT_INPUT:
        if (app.wantsText() && e.text.text) app.handleInput(ui::InputEvent::typed(e.text.text));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const ui::Point p = toCanvas(e.button.x, e.button.y);
        mouseDown_ = true;
        app.handleInput(ui::InputEvent::pointer(T::PointerDown, p));
        if (e.button.button == SDL_BUTTON_RIGHT) {  // right click = long press
            app.handleInput(ui::InputEvent::pointer(T::LongPress, p));
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!mouseDown_) break;
        mouseDown_ = false;
        app.handleInput(ui::InputEvent::pointer(T::PointerUp, toCanvas(e.button.x, e.button.y)));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (mouseDown_) app.handleInput(ui::InputEvent::pointer(T::PointerMove, toCanvas(e.motion.x, e.motion.y)));
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (SDL_GetModState() & SDL_KMOD_CTRL) setScale(scale_ * (e.wheel.y > 0 ? 1.1 : 1 / 1.1));
        break;
    default: break;
    }
}

int SdlHost::run(app::Application& app) {
    running_ = true;
    app.handleLifecycle(Lifecycle::Show);
    SDL_Event e;
    while (running_ && SDL_WaitEvent(&e)) {  // event-driven, no polling (design notes §31.5)
        handleEvent(app, e);
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------
// SdlPresenter

SdlPresenter::SdlPresenter(SDL_Window* window, SDL_Renderer* renderer, DeviceProfile profile, SdlHost& host)
    : window_(window), renderer_(renderer), profile_(std::move(profile)), host_(host), sim_(profile_.width, profile_.height) {
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, profile_.width, profile_.height);
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    if (!profile_.color) sim_.setMode(EinkSimulator::Mode::Mono16);
}

SdlPresenter::~SdlPresenter() {
    if (repaintTimer_) host_.cancelTimer(repaintTimer_);
    if (texture_) SDL_DestroyTexture(texture_);
}

InfoList SdlPresenter::info() const {
    const char* name = SDL_GetRendererName(renderer_);
    return {{"Presenter", std::string("SDL3 renderer ") + (name ? name : "?")},
            {"Simulator", std::string(EinkSimulator::modeName(sim_.mode())) + (latency_ ? ", latency on" : "")}};
}

void SdlPresenter::updateTitle() {
    char title[160];
    std::snprintf(title, sizeof title, "PocketChess  |  %s  |  fast %d / partial %d / full %d%s", EinkSimulator::modeName(sim_.mode()),
                  counts_[0], counts_[1], counts_[2], latency_ ? "  |  latency" : "");
    SDL_SetWindowTitle(window_, title);
}

void SdlPresenter::cycleDisplayMode(const ui::Canvas& canvas) {
    sim_.cycleMode();
    sim_.applyAll(canvas);
    SDL_UpdateTexture(texture_, nullptr, sim_.panel().data(), profile_.width * 3);
    updateTitle();
    repaint();
}

void SdlPresenter::present(const ui::Canvas& canvas, const ui::PresentRequest& req) {
    const Uint64 t0 = SDL_GetTicksNS();
    ++counts_[static_cast<int>(req.mode)];
    for (const ui::Rect& r : req.rects) {
        sim_.apply(canvas, r);
        const SDL_Rect sr{r.x, r.y, r.w, r.h};
        const uint8_t* src = sim_.panel().data() + (static_cast<size_t>(r.y) * static_cast<size_t>(profile_.width) + static_cast<size_t>(r.x)) * 3;
        SDL_UpdateTexture(texture_, &sr, src, profile_.width * 3);
        if (overlay_) overlays_.push_back({r, req.mode, SDL_GetTicks() + 300});
    }
    if (flash_ && req.mode == ui::RefreshMode::Full) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderPresent(renderer_);
        SDL_Delay(90);
    }
    if (latency_) {
        static const Uint32 delay[3] = {120, 450, 900};  // tune from device measurements (M0.5)
        SDL_Delay(delay[static_cast<int>(req.mode)]);
    }
    repaint();
    updateTitle();
    lastMs_ = static_cast<double>(SDL_GetTicksNS() - t0) / 1e6;
    if (!overlays_.empty() && !repaintTimer_) {
        repaintTimer_ = host_.startTimer(std::chrono::milliseconds(320), [this] {
            repaintTimer_ = 0;
            repaint();
        });
    }
}

void SdlPresenter::repaint() {
    int ow = 1, oh = 1;
    SDL_GetCurrentRenderOutputSize(renderer_, &ow, &oh);
    SDL_SetRenderDrawColor(renderer_, 60, 60, 60, 255);
    SDL_RenderClear(renderer_);
    const SDL_FRect dst{0, 0, static_cast<float>(ow), static_cast<float>(oh)};
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);

    const Uint64 now = SDL_GetTicks();
    overlays_.erase(std::remove_if(overlays_.begin(), overlays_.end(), [&](const Overlay& o) { return o.until <= now; }),
                    overlays_.end());
    const float sx = static_cast<float>(ow) / static_cast<float>(profile_.width);
    const float sy = static_cast<float>(oh) / static_cast<float>(profile_.height);
    for (const Overlay& o : overlays_) {
        switch (o.mode) {
        case ui::RefreshMode::Fast: SDL_SetRenderDrawColor(renderer_, 30, 144, 255, 255); break;   // blue
        case ui::RefreshMode::Partial: SDL_SetRenderDrawColor(renderer_, 0, 170, 0, 255); break;   // green
        case ui::RefreshMode::Full: SDL_SetRenderDrawColor(renderer_, 220, 30, 30, 255); break;    // red
        }
        for (int k = 0; k < 3; ++k) {
            const float fk = static_cast<float>(k);
            const SDL_FRect fr{static_cast<float>(o.rect.x) * sx + fk, static_cast<float>(o.rect.y) * sy + fk,
                               static_cast<float>(o.rect.w) * sx - 2.0f * fk, static_cast<float>(o.rect.h) * sy - 2.0f * fk};
            SDL_RenderRect(renderer_, &fr);
        }
    }
    SDL_RenderPresent(renderer_);
}

} // namespace pchess::platform::sdl
