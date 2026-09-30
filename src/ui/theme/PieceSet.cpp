// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme/PieceSet.h"

#include "ui/canvas/Rasterizer.h"
#include "ui/theme/Palette.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pchess::ui {
namespace {

// U+2654..2659 = white K Q R B N P, U+265A..265F = black.
char32_t glyphFor(chess::Piece p) {
    using namespace chess;
    static const int order[6] = {5, 4, 3, 2, 1, 0};  // Pawn..King -> offset from King
    const int base = colorOf(p) == White ? 0x2654 : 0x265A;
    return static_cast<char32_t>(base + order[typeOf(p)]);
}

} // namespace

GlyphPieceSet::GlyphPieceSet(std::shared_ptr<const TrueTypeFont> face, std::string name)
    : face_(std::move(face)), name_(std::move(name)) {}

bool GlyphPieceSet::complete() const {
    for (int p = 0; p < 12; ++p)
        if (face_->glyphIndex(glyphFor(static_cast<chess::Piece>(p))) == 0) return false;
    return true;
}

Bitmap GlyphPieceSet::render(chess::Piece piece, int px) const {
    Bitmap out(px, px);
    const int glyph = face_->glyphIndex(glyphFor(piece));
    std::vector<Contour> contours;
    int x0, y0, x1, y1;
    if (!glyph || !face_->outline(glyph, contours) || !face_->bounds(glyph, x0, y0, x1, y1)) return out;

    // One scale for the whole set, from the king's height, so pieces keep their proportions;
    // every piece stands on a common base line.
    int kx0, ky0, kx1, ky1;
    const int king = face_->glyphIndex(glyphFor(chess::WKing));
    if (!face_->bounds(king, kx0, ky0, kx1, ky1)) {
        kx0 = x0; ky0 = y0; kx1 = x1; ky1 = y1;
    }
    const float scale = 0.84f * static_cast<float>(px) / static_cast<float>(std::max(ky1 - ky0, kx1 - kx0));
    const float baseline = std::round(0.92f * static_cast<float>(px));
    const float glyphW = static_cast<float>(x1 - x0) * scale;
    const float left = std::round((static_cast<float>(px) - glyphW) * 0.5f) - static_cast<float>(x0) * scale;
    const float top = baseline + static_cast<float>(y0) * scale;  // y up -> down, bottom on baseline

    Rasterizer r(px, px);
    r.addContours(contours, scale, -scale, left, top);
    const AlphaMask ink = r.finish();

    // Flood-fill the outside (4-connected, through weakly covered pixels); the rest is the body.
    const size_t n = static_cast<size_t>(px) * static_cast<size_t>(px);
    std::vector<uint8_t> outside(n, 0);
    std::vector<int> stack;
    auto seed = [&](int x, int y) {
        const size_t i = static_cast<size_t>(y) * static_cast<size_t>(px) + static_cast<size_t>(x);
        if (!outside[i] && ink.alpha[i] < 160) {
            outside[i] = 1;
            stack.push_back(static_cast<int>(i));
        }
    };
    for (int i = 0; i < px; ++i) {
        seed(i, 0);
        seed(i, px - 1);
        seed(0, i);
        seed(px - 1, i);
    }
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        const int x = i % px, y = i / px;
        if (x > 0) seed(x - 1, y);
        if (x + 1 < px) seed(x + 1, y);
        if (y > 0) seed(x, y - 1);
        if (y + 1 < px) seed(x, y + 1);
    }

    for (int y = 0; y < px; ++y) {
        for (int x = 0; x < px; ++x) {
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(px) + static_cast<size_t>(x);
            const uint32_t c = ink.alpha[i];
            uint8_t* d = out.px(x, y);
            if (!outside[i]) {
                const uint8_t v = static_cast<uint8_t>(255 - c);  // white body, black ink
                d[0] = d[1] = d[2] = v;
                d[3] = 255;
            } else {
                d[0] = d[1] = d[2] = 0;
                d[3] = static_cast<uint8_t>(c);
            }
        }
    }
    return out;
}

const Bitmap& PieceCache::get(chess::Piece piece, int px) {
    auto& slot = cache_[px];
    auto& have = have_[px];
    const int i = static_cast<int>(piece);
    if (!have[static_cast<size_t>(i)]) {
        slot[static_cast<size_t>(i)] = set_ ? set_->render(piece, px) : Bitmap(px, px);
        if (tint_) {
            Bitmap& b = slot[static_cast<size_t>(i)];
            b = chess::colorOf(piece) == chess::White ? recolor(b, tint_->outline, tint_->white)
                                                      : recolor(b, tint_->black, tint_->white);
        }
        have[static_cast<size_t>(i)] = true;
    }
    return slot[static_cast<size_t>(i)];
}

size_t PieceCache::bytes() const {
    size_t total = 0;
    for (const auto& [px, arr] : cache_)
        for (const auto& b : arr) total += b.rgba.size();
    return total;
}

} // namespace pchess::ui
