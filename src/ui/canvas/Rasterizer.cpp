// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/canvas/Rasterizer.h"

#include <algorithm>
#include <cmath>

namespace pchess::ui {

Rasterizer::Rasterizer(int width, int height)
    : w_(width), h_(height), acc_(static_cast<size_t>(width) * static_cast<size_t>(height) + 4, 0.0f) {}

void Rasterizer::moveTo(PointF p) {
    if (open_) close();
    start_ = cur_ = p;
    open_ = true;
}

void Rasterizer::lineTo(PointF p) {
    line(cur_, p);
    cur_ = p;
}

void Rasterizer::quadTo(PointF c, PointF p) {
    const float ddx = cur_.x - 2 * c.x + p.x;
    const float ddy = cur_.y - 2 * c.y + p.y;
    const float dd = std::sqrt(ddx * ddx + ddy * ddy);
    const int n = 1 + static_cast<int>(std::floor(std::sqrt(std::sqrt(3.0f * dd))));
    const PointF p0 = cur_;
    const float inv = 1.0f / static_cast<float>(n);
    for (int i = 1; i < n; ++i) {
        const float t = static_cast<float>(i) * inv;
        const float mt = 1.0f - t;
        const float a = mt * mt, b = 2.0f * mt * t, d = t * t;
        lineTo({a * p0.x + b * c.x + d * p.x, a * p0.y + b * c.y + d * p.y});
    }
    lineTo(p);
}

void Rasterizer::close() {
    if (!open_) return;
    if (cur_.x != start_.x || cur_.y != start_.y) line(cur_, start_);
    cur_ = start_;
    open_ = false;
}

void Rasterizer::line(PointF p0, PointF p1) {
    if (p0.y == p1.y) return;
    float dir = 1.0f;
    if (p0.y > p1.y) {
        dir = -1.0f;
        std::swap(p0, p1);
    }
    const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    float x = p0.x;
    float y0 = p0.y;
    if (y0 < 0.0f) {
        x -= y0 * dxdy;
        y0 = 0.0f;
    }
    const float y1 = std::min(p1.y, static_cast<float>(h_));
    if (y0 >= y1) return;
    const int yStart = static_cast<int>(y0);
    const int yEnd = static_cast<int>(std::ceil(y1));
    for (int y = yStart; y < yEnd; ++y) {
        const size_t ls = static_cast<size_t>(y) * static_cast<size_t>(w_);
        const float dy = std::min(static_cast<float>(y + 1), y1) - std::max(static_cast<float>(y), y0);
        const float xnext = x + dxdy * dy;
        const float d = dy * dir;
        float xa = x, xb = xnext;
        if (xa > xb) std::swap(xa, xb);
        // Clamp horizontally: coverage left of the canvas collapses onto column 0.
        xa = std::clamp(xa, 0.0f, static_cast<float>(w_));
        xb = std::clamp(xb, 0.0f, static_cast<float>(w_));
        const float x0floor = std::floor(xa);
        const int x0i = static_cast<int>(x0floor);
        const float x1ceil = std::ceil(xb);
        const int x1i = static_cast<int>(x1ceil);
        auto add = [&](int xi, float v) {
            if (xi >= 0 && xi <= w_) acc_[ls + static_cast<size_t>(xi)] += v;
        };
        if (x1i <= x0i + 1) {
            const float xmf = 0.5f * (xa + xb) - x0floor;
            add(x0i, d - d * xmf);
            add(x0i + 1, d * xmf);
        } else {
            const float s = 1.0f / (xb - xa);
            const float x0f = xa - x0floor;
            const float a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
            const float x1f = xb - x1ceil + 1.0f;
            const float am = 0.5f * s * x1f * x1f;
            add(x0i, d * a0);
            if (x1i == x0i + 2) {
                add(x0i + 1, d * (1.0f - a0 - am));
            } else {
                const float a1 = s * (1.5f - x0f);
                add(x0i + 1, d * (a1 - a0));
                for (int xi = x0i + 2; xi < x1i - 1; ++xi) add(xi, d * s);
                const float a2 = a1 + static_cast<float>(x1i - x0i - 3) * s;
                add(x1i - 1, d * (1.0f - a2 - am));
            }
            add(x1i, d * am);
        }
        x = xnext;
    }
}

void Rasterizer::addContours(const std::vector<Contour>& contours, float sx, float sy, float dx, float dy) {
    auto tf = [&](PointF p) { return PointF{p.x * sx + dx, p.y * sy + dy}; };
    for (const Contour& c : contours) {
        const size_t n = c.points.size();
        if (n < 2) continue;
        // Find a starting on-curve point (or the midpoint of the first two off-curve points).
        size_t first = 0;
        while (first < n && !c.onCurve[first]) ++first;
        PointF startPt;
        size_t startIdx;
        if (first == n) {
            startPt = {(c.points[0].x + c.points[1].x) * 0.5f, (c.points[0].y + c.points[1].y) * 0.5f};
            startIdx = 1;
        } else {
            startPt = c.points[first];
            startIdx = first;
        }
        moveTo(tf(startPt));
        bool haveControl = false;
        PointF control{};
        for (size_t k = 1; k <= n; ++k) {
            const size_t i = (startIdx + k) % n;
            const PointF p = c.points[i];
            if (c.onCurve[i]) {
                if (haveControl) quadTo(tf(control), tf(p));
                else lineTo(tf(p));
                haveControl = false;
            } else {
                if (haveControl) {
                    const PointF mid{(control.x + p.x) * 0.5f, (control.y + p.y) * 0.5f};
                    quadTo(tf(control), tf(mid));
                }
                control = p;
                haveControl = true;
            }
        }
        if (haveControl) quadTo(tf(control), tf(startPt));
        close();
    }
}

AlphaMask Rasterizer::finish() const {
    AlphaMask m;
    m.width = w_;
    m.height = h_;
    m.alpha.resize(static_cast<size_t>(w_) * static_cast<size_t>(h_));
    float sum = 0.0f;
    for (size_t i = 0; i < m.alpha.size(); ++i) {
        sum += acc_[i];
        // Row ends: carry over is intentional (coverage right of the last column is 0 once all
        // contours are closed, so the running sum returns to ~0 at every row end).
        const float a = std::min(1.0f, std::fabs(sum));
        m.alpha[i] = static_cast<uint8_t>(a * 255.0f + 0.5f);
    }
    return m;
}

} // namespace pchess::ui
