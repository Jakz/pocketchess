// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sdl/EinkSimulator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

namespace pchess::platform::sdl {

EinkSimulator::EinkSimulator(int width, int height)
    : w_(width), h_(height), panel_(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 255) {}

EinkSimulator::Mode EinkSimulator::cycleMode() {
    mode_ = mode_ == Mode::Exact ? Mode::Mono16 : (mode_ == Mode::Mono16 ? Mode::Kaleido : Mode::Exact);
    return mode_;
}

const char* EinkSimulator::modeName(Mode m) {
    switch (m) {
    case Mode::Exact: return "Exact RGB";
    case Mode::Mono16: return "Mono 16-gray";
    case Mode::Kaleido: return "Kaleido 3 approx.";
    }
    return "?";
}

void EinkSimulator::loadParams(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
        const float v = std::strtof(line.c_str() + colon + 1, nullptr);
        if (key == "saturation") params_.saturation = v;
        else if (key == "darken") params_.darken = v;
        else if (key == "gamma") params_.gamma = v;
        else if (key == "chroma_block") params_.chromaBlock = std::max(1, static_cast<int>(v));
    }
}

void EinkSimulator::apply(const ui::Canvas& canvas, const ui::Rect& in) {
    const ui::Rect r = in.intersected(canvas.bounds());
    if (r.empty()) return;
    auto out = [&](int x, int y) { return &panel_[(static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)) * 3]; };

    if (mode_ == Mode::Exact) {
        for (int y = r.y; y < r.bottom(); ++y)
            std::copy_n(canvas.row(y) + r.x * 3, r.w * 3, out(r.x, y));
        return;
    }
    if (mode_ == Mode::Mono16) {
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x) {
                const uint8_t l = canvas.pixel(x, y).luma();
                const uint8_t q = static_cast<uint8_t>((l * 15 + 127) / 255 * 17);
                uint8_t* p = out(x, y);
                p[0] = p[1] = p[2] = q;
            }
        return;
    }
    // Kaleido: luminance per pixel (300 ppi), chroma averaged per block (150 ppi), muted.
    const int b = params_.chromaBlock;
    const int x0 = r.x / b * b, y0 = r.y / b * b;
    for (int by = y0; by < r.bottom(); by += b) {
        for (int bx = x0; bx < r.right(); bx += b) {
            float cr = 0, cg = 0, cb = 0;
            int n = 0;
            for (int y = by; y < std::min(by + b, h_); ++y)
                for (int x = bx; x < std::min(bx + b, w_); ++x) {
                    const ui::Rgb c = canvas.pixel(x, y);
                    const float l = c.luma();
                    cr += c.r - l;
                    cg += c.g - l;
                    cb += c.b - l;
                    ++n;
                }
            if (n) {
                cr /= static_cast<float>(n);
                cg /= static_cast<float>(n);
                cb /= static_cast<float>(n);
            }
            for (int y = by; y < std::min(by + b, h_); ++y)
                for (int x = bx; x < std::min(bx + b, w_); ++x) {
                    if (!r.contains(ui::Point{x, y})) continue;
                    const float l = 255.0f * std::pow(canvas.pixel(x, y).luma() / 255.0f, params_.gamma) * params_.darken;
                    uint8_t* p = out(x, y);
                    p[0] = static_cast<uint8_t>(std::clamp(l + cr * params_.saturation, 0.0f, 255.0f));
                    p[1] = static_cast<uint8_t>(std::clamp(l + cg * params_.saturation, 0.0f, 255.0f));
                    p[2] = static_cast<uint8_t>(std::clamp(l + cb * params_.saturation, 0.0f, 255.0f));
                }
        }
    }
}

} // namespace pchess::platform::sdl
