// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Color.h"
#include "ui/canvas/Geometry.h"

#include <cstdint>
#include <vector>

namespace pchess::ui {

// 8-bit coverage mask (glyphs, shapes). (left, top) is the offset of pixel (0,0) from the
// drawing origin (for glyphs: from the pen position on the baseline).
struct AlphaMask {
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    std::vector<uint8_t> alpha;

    uint8_t at(int x, int y) const { return alpha[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; }
};

// Straight (non-premultiplied) RGBA image, e.g. a piece bitmap.
struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    Bitmap() = default;
    Bitmap(int w, int h) : width(w), height(h), rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0) {}
    uint8_t* px(int x, int y) { return &rgba[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4]; }
    const uint8_t* px(int x, int y) const {
        return &rgba[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4];
    }
};

// Platform-independent 24-bit RGB drawing surface at device resolution (architecture doc §3).
// All drawing is integer-only, so the same calls give the same pixels on every platform;
// golden-image tests compare canvas hashes exactly.
class Canvas {
public:
    Canvas() = default;
    Canvas(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    Rect bounds() const { return {0, 0, width_, height_}; }
    int stride() const { return width_ * 3; }
    const uint8_t* data() const { return pixels_.data(); }
    const uint8_t* row(int y) const { return pixels_.data() + static_cast<size_t>(y) * static_cast<size_t>(stride()); }
    Rgb pixel(int x, int y) const;

    // Clipping (intersected with the current clip).
    void pushClip(const Rect& r);
    void popClip();
    Rect clip() const { return clip_; }

    void fill(const Rect& r, Rgb c);
    void blendFill(const Rect& r, Rgb c, uint8_t alpha);
    void frame(const Rect& r, int thickness, Rgb c);
    void dottedFrame(const Rect& r, int thickness, int dash, Rgb c);
    // Diagonal hatch lines inside r (mono selection pattern, design notes §17.3).
    void hatch(const Rect& r, int spacing, int thickness, Rgb c);
    void hline(int x0, int x1, int y, int thickness, Rgb c);
    void vline(int x, int y0, int y1, int thickness, Rgb c);
    // Anti-aliased (4x4 supersampled) discs and rings.
    void fillCircle(int cx, int cy, int radius, Rgb c);
    void ring(int cx, int cy, int outerRadius, int innerRadius, Rgb c);

    void drawMask(const AlphaMask& m, int x, int y, Rgb c);
    void blit(const Bitmap& b, int x, int y);

    // Converts r to gray in place (used by the mono display mode preview/tests).
    void toGray(const Rect& r);

    // FNV-1a 64 over the pixels (golden checksums on desktop and device).
    uint64_t hash() const;
    uint64_t hash(const Rect& r) const;

private:
    uint8_t* at(int x, int y) { return pixels_.data() + (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 3; }
    void put(int x, int y, Rgb c) {
        uint8_t* p = at(x, y);
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
    }
    void putBlend(int x, int y, Rgb c, uint32_t a);
    void ringImpl(int cx, int cy, int outer, int inner, Rgb c);

    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels_;
    Rect clip_;
    std::vector<Rect> clipStack_;
};

} // namespace pchess::ui
