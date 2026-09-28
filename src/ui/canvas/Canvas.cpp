// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/canvas/Canvas.h"

#include <cstdlib>

namespace pchess::ui {

Canvas::Canvas(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 255),
      clip_{0, 0, width, height} {}

Rgb Canvas::pixel(int x, int y) const {
    const uint8_t* p = pixels_.data() + (static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)) * 3;
    return {p[0], p[1], p[2]};
}

void Canvas::pushClip(const Rect& r) {
    clipStack_.push_back(clip_);
    clip_ = clip_.intersected(r);
}

void Canvas::popClip() {
    if (clipStack_.empty()) return;
    clip_ = clipStack_.back();
    clipStack_.pop_back();
}

void Canvas::putBlend(int x, int y, Rgb c, uint32_t a) {
    if (a == 0) return;
    uint8_t* p = at(x, y);
    if (a >= 255) {
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        return;
    }
    p[0] = blend8(p[0], c.r, a);
    p[1] = blend8(p[1], c.g, a);
    p[2] = blend8(p[2], c.b, a);
}

void Canvas::fill(const Rect& r, Rgb c) {
    const Rect a = r.intersected(clip_);
    if (a.empty()) return;
    for (int y = a.y; y < a.bottom(); ++y) {
        uint8_t* p = at(a.x, y);
        for (int x = 0; x < a.w; ++x) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p += 3;
        }
    }
}

void Canvas::blendFill(const Rect& r, Rgb c, uint8_t alpha) {
    const Rect a = r.intersected(clip_);
    for (int y = a.y; y < a.bottom(); ++y)
        for (int x = a.x; x < a.right(); ++x) putBlend(x, y, c, alpha);
}

void Canvas::frame(const Rect& r, int t, Rgb c) {
    if (r.empty() || t <= 0) return;
    t = std::min(t, std::min(r.w, r.h) / 2 + 1);
    fill({r.x, r.y, r.w, t}, c);
    fill({r.x, r.bottom() - t, r.w, t}, c);
    fill({r.x, r.y + t, t, r.h - 2 * t}, c);
    fill({r.right() - t, r.y + t, t, r.h - 2 * t}, c);
}

void Canvas::dottedFrame(const Rect& r, int t, int dash, Rgb c) {
    if (r.empty() || t <= 0 || dash <= 0) return;
    for (int x = r.x; x < r.right(); x += 2 * dash) {
        const int w = std::min(dash, r.right() - x);
        fill({x, r.y, w, t}, c);
        fill({x, r.bottom() - t, w, t}, c);
    }
    for (int y = r.y; y < r.bottom(); y += 2 * dash) {
        const int h = std::min(dash, r.bottom() - y);
        fill({r.x, y, t, h}, c);
        fill({r.right() - t, y, t, h}, c);
    }
}

void Canvas::hatch(const Rect& r, int spacing, int thickness, Rgb c) {
    const Rect a = r.intersected(clip_);
    if (a.empty() || spacing <= 0) return;
    for (int y = a.y; y < a.bottom(); ++y)
        for (int x = a.x; x < a.right(); ++x) {
            const int d = ((x - r.x) + (y - r.y)) % spacing;
            if (d < thickness) put(x, y, c);
        }
}

void Canvas::hline(int x0, int x1, int y, int t, Rgb c) { fill({std::min(x0, x1), y, std::abs(x1 - x0), t}, c); }
void Canvas::vline(int x, int y0, int y1, int t, Rgb c) { fill({x, std::min(y0, y1), t, std::abs(y1 - y0)}, c); }

void Canvas::ringImpl(int cx, int cy, int outer, int inner, Rgb c) {
    // 4x4 supersampling with integer distance tests (sample centres at 1/8, 3/8, ...).
    const Rect box = Rect{cx - outer - 1, cy - outer - 1, 2 * outer + 3, 2 * outer + 3}.intersected(clip_);
    const int64_t o2 = int64_t{outer} * outer * 64;  // radii in 1/8 px units squared
    const int64_t i2 = int64_t{inner} * inner * 64;
    for (int y = box.y; y < box.bottom(); ++y) {
        for (int x = box.x; x < box.right(); ++x) {
            int cover = 0;
            for (int sy = 0; sy < 4; ++sy) {
                const int64_t dy = int64_t{y - cy} * 8 + 1 + 2 * sy - 4;
                for (int sx = 0; sx < 4; ++sx) {
                    const int64_t dx = int64_t{x - cx} * 8 + 1 + 2 * sx - 4;
                    const int64_t d2 = dx * dx + dy * dy;
                    if (d2 <= o2 && (inner <= 0 || d2 > i2)) ++cover;
                }
            }
            if (cover) putBlend(x, y, c, static_cast<uint32_t>(cover * 255 / 16));
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, Rgb c) { ringImpl(cx, cy, radius, 0, c); }
void Canvas::ring(int cx, int cy, int outer, int inner, Rgb c) { ringImpl(cx, cy, outer, inner, c); }

void Canvas::drawMask(const AlphaMask& m, int x, int y, Rgb c) {
    const Rect dst = Rect{x + m.left, y + m.top, m.width, m.height}.intersected(clip_);
    for (int py = dst.y; py < dst.bottom(); ++py) {
        const int my = py - (y + m.top);
        for (int px = dst.x; px < dst.right(); ++px) putBlend(px, py, c, m.at(px - (x + m.left), my));
    }
}

void Canvas::blit(const Bitmap& b, int x, int y) {
    const Rect dst = Rect{x, y, b.width, b.height}.intersected(clip_);
    for (int py = dst.y; py < dst.bottom(); ++py) {
        for (int px = dst.x; px < dst.right(); ++px) {
            const uint8_t* s = b.px(px - x, py - y);
            putBlend(px, py, {s[0], s[1], s[2]}, s[3]);
        }
    }
}

void Canvas::toGray(const Rect& r) {
    const Rect a = r.intersected(bounds());
    for (int y = a.y; y < a.bottom(); ++y)
        for (int x = a.x; x < a.right(); ++x) put(x, y, pixel(x, y).toGray());
}

uint64_t Canvas::hash() const { return hash(bounds()); }

uint64_t Canvas::hash(const Rect& r) const {
    uint64_t h = 1469598103934665603ULL;
    const Rect a = r.intersected(bounds());
    for (int y = a.y; y < a.bottom(); ++y) {
        const uint8_t* p = row(y) + a.x * 3;
        for (int i = 0; i < a.w * 3; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

} // namespace pchess::ui
