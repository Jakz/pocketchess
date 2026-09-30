// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Geometry.h"

#include <cstdint>
#include <vector>

namespace pchess::ui {

// E-ink refresh modes (design notes §22). Backends map them to device calls:
// Fast -> fast monochrome waveform (tap feedback, selection), Partial -> normal partial update,
// Full -> flashing full refresh (screen change, ghosting cleanup).
enum class RefreshMode : uint8_t { Fast = 0, Partial = 1, Full = 2 };

// What kind of change a view reports; the policy turns hints into a mode.
enum class RefreshHint : uint8_t {
    Selection = 0,  // transient marker (selection, pressed button)
    Content = 1,    // a move, new text
    Page = 2,       // a large content change that stays partial (list page turns, M5)
    Screen = 3,     // new screen, theme change, big dialog
};

const char* toString(RefreshMode m);

struct PresentRequest {
    std::vector<Rect> rects;  // canvas rectangles to push to the panel
    RefreshMode mode = RefreshMode::Partial;
    bool fullScreen = false;
    bool colorContent = true;  // false: the region is pure gray, the backend may use a mono call
};

// Collects invalid rectangles and coalesces them (ported idea from Enigmistica's DirtyRegions):
// merge two rects when their union is not much larger than the pair; beyond maxRects fall back
// to the bounding box.
class DirtyRegions {
public:
    explicit DirtyRegions(Rect bounds = {}, size_t maxRects = 12) : bounds_(bounds), maxRects_(maxRects) {}
    void setBounds(Rect b) { bounds_ = b; }
    void add(const Rect& r);
    void addAll() { add(bounds_); }
    bool empty() const { return rects_.empty(); }
    const std::vector<Rect>& rects() const { return rects_; }
    Rect boundingBox() const;
    std::vector<Rect> take();

private:
    Rect bounds_;
    size_t maxRects_;
    std::vector<Rect> rects_;
};

// Chooses the refresh mode: Screen hints and large areas -> Full; every `fullEvery` content
// updates -> Full (ghosting cleanup, design notes §22.2); content -> Partial; selection-only ->
// Fast.
class RefreshPolicy {
public:
    void setFullEvery(int n) { fullEvery_ = n; }
    int fullEvery() const { return fullEvery_; }
    RefreshMode decide(RefreshHint strongestHint, int64_t dirtyArea, int64_t screenArea);
    void resetCounter() { contentSinceFull_ = 0; }
    int contentSinceFull() const { return contentSinceFull_; }

private:
    int fullEvery_ = 12;
    int contentSinceFull_ = 0;
};

} // namespace pchess::ui
