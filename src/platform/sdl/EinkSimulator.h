// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Canvas.h"
#include "ui/render/Refresh.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pchess::platform::sdl {

// Converts canvas pixels into what the panel would show (architecture doc §4). Pure CPU code,
// no SDL: the SdlPresenter uploads `panel()` to a texture.
class EinkSimulator {
public:
    enum class Mode { Exact, Mono16, Kaleido };

    struct KaleidoParams {
        float saturation = 0.55f;   // chroma kept (Kaleido 3 is muted)
        float darken = 0.90f;       // white point of the color layer
        float gamma = 1.10f;        // luminance response
        int chromaBlock = 2;        // 300 ppi luminance vs 150 ppi color
    };

    explicit EinkSimulator(int width, int height);

    void setMode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    Mode cycleMode();
    static const char* modeName(Mode m);

    // Loads `key: value` lines (saturation, darken, gamma, chroma_block); missing file = defaults.
    void loadParams(const std::filesystem::path& file);
    const KaleidoParams& params() const { return params_; }

    // Updates the simulated panel in `r` from the canvas.
    void apply(const ui::Canvas& canvas, const ui::Rect& r);
    void applyAll(const ui::Canvas& canvas) { apply(canvas, canvas.bounds()); }

    const std::vector<uint8_t>& panel() const { return panel_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    int w_, h_;
    Mode mode_ = Mode::Exact;
    KaleidoParams params_;
    std::vector<uint8_t> panel_;  // RGB24
};

} // namespace pchess::platform::sdl
