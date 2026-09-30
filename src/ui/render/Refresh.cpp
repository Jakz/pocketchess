// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/render/Refresh.h"

namespace pchess::ui {

const char* toString(RefreshMode m) {
    switch (m) {
    case RefreshMode::Fast: return "fast";
    case RefreshMode::Partial: return "partial";
    case RefreshMode::Full: return "full";
    }
    return "?";
}

void DirtyRegions::add(const Rect& in) {
    Rect r = bounds_.empty() ? in : in.intersected(bounds_);
    if (r.empty()) return;
    // Merge repeatedly while a merge is "cheap": union area <= 1.25 x (sum of areas).
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < rects_.size(); ++i) {
            const Rect u = rects_[i].united(r);
            if (rects_[i].contains(r)) return;
            if (u.area() * 4 <= (rects_[i].area() + r.area()) * 5 || rects_[i].intersects(r)) {
                r = u;
                rects_.erase(rects_.begin() + static_cast<std::ptrdiff_t>(i));
                merged = true;
                break;
            }
        }
    }
    rects_.push_back(r);
    if (rects_.size() > maxRects_) {
        const Rect bb = boundingBox();
        rects_.clear();
        rects_.push_back(bb);
    }
}

Rect DirtyRegions::boundingBox() const {
    Rect bb;
    for (const Rect& r : rects_) bb = bb.united(r);
    return bb;
}

std::vector<Rect> DirtyRegions::take() {
    std::vector<Rect> out;
    out.swap(rects_);
    return out;
}

RefreshMode RefreshPolicy::decide(RefreshHint hint, int64_t dirtyArea, int64_t screenArea) {
    // Large updates flash a full refresh against ghosting, unless the screen asked for a page turn
    // (a list page is large but should stay partial; the periodic full still cleans up).
    const bool large = hint != RefreshHint::Page && screenArea > 0 && dirtyArea * 2 > screenArea;
    if (hint == RefreshHint::Screen || large) {
        contentSinceFull_ = 0;
        return RefreshMode::Full;
    }
    if (hint == RefreshHint::Selection) return RefreshMode::Fast;
    if (fullEvery_ > 0 && ++contentSinceFull_ >= fullEvery_) {
        contentSinceFull_ = 0;
        return RefreshMode::Full;
    }
    return RefreshMode::Partial;
}

} // namespace pchess::ui
