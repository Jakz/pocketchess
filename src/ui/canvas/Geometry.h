// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace pchess::ui {

struct Point {
    int x = 0;
    int y = 0;
    constexpr bool operator==(const Point&) const = default;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr bool empty() const { return w <= 0 || h <= 0; }
    constexpr int64_t area() const { return empty() ? 0 : int64_t{w} * h; }
    constexpr bool contains(Point p) const { return p.x >= x && p.y >= y && p.x < right() && p.y < bottom(); }
    constexpr bool contains(const Rect& r) const {
        return r.x >= x && r.y >= y && r.right() <= right() && r.bottom() <= bottom();
    }
    constexpr bool intersects(const Rect& r) const {
        return !empty() && !r.empty() && r.x < right() && x < r.right() && r.y < bottom() && y < r.bottom();
    }
    constexpr Rect intersected(const Rect& r) const {
        const int nx = std::max(x, r.x), ny = std::max(y, r.y);
        const int nr = std::min(right(), r.right()), nb = std::min(bottom(), r.bottom());
        if (nr <= nx || nb <= ny) return {};
        return {nx, ny, nr - nx, nb - ny};
    }
    constexpr Rect united(const Rect& r) const {
        if (empty()) return r;
        if (r.empty()) return *this;
        const int nx = std::min(x, r.x), ny = std::min(y, r.y);
        return {nx, ny, std::max(right(), r.right()) - nx, std::max(bottom(), r.bottom()) - ny};
    }
    constexpr Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    constexpr Rect inset(int dx, int dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    constexpr Point center() const { return {x + w / 2, y + h / 2}; }
    constexpr bool operator==(const Rect&) const = default;
};

} // namespace pchess::ui
