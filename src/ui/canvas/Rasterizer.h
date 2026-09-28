// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Canvas.h"

#include <vector>

namespace pchess::ui {

struct PointF {
    float x = 0;
    float y = 0;
};

// One closed contour of quadratic Bézier segments, TrueType style: `onCurve[i]` tells whether
// points[i] is on the curve; two consecutive off-curve points imply an on-curve midpoint.
struct Contour {
    std::vector<PointF> points;
    std::vector<bool> onCurve;
};

// Anti-aliased path rasterizer (signed-area accumulation, as in font-rs). Winding contours
// accumulate; coverage is |sum| clamped to 1. Builds with -ffp-contract=off (and /fp:precise on
// MSVC) keep the float math bit-identical on x64 and ARM, which the golden checksums rely on.
class Rasterizer {
public:
    Rasterizer(int width, int height);

    void moveTo(PointF p);
    void lineTo(PointF p);
    void quadTo(PointF control, PointF p);
    void close();

    // Adds contours transformed by (x * sx + dx, y * sy + dy).
    void addContours(const std::vector<Contour>& contours, float sx, float sy, float dx, float dy);

    // Coverage mask of everything added so far (width x height, left/top = 0).
    AlphaMask finish() const;

private:
    void line(PointF a, PointF b);

    int w_, h_;
    std::vector<float> acc_;
    PointF start_{}, cur_{};
    bool open_ = false;
};

} // namespace pchess::ui
