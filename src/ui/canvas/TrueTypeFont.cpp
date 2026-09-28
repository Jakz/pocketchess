// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/canvas/TrueTypeFont.h"

#include <cstdio>

namespace pchess::ui {

Result<std::shared_ptr<TrueTypeFont>> TrueTypeFont::load(const std::filesystem::path& file) {
    std::FILE* f = std::fopen(file.string().c_str(), "rb");
    if (!f) return makeError(ErrorCode::NotFound, "font " + file.string());
    std::vector<uint8_t> bytes;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    auto r = fromBytes(std::move(bytes));
    if (!r) return makeError(r.error().code, r.error().context + " (" + file.string() + ")");
    return r;
}

Result<std::shared_ptr<TrueTypeFont>> TrueTypeFont::fromBytes(std::vector<uint8_t> bytes) {
    std::shared_ptr<TrueTypeFont> font(new TrueTypeFont());
    font->data_ = std::move(bytes);
    if (auto r = font->parse(); !r) return r.error();
    return font;
}

Result<void> TrueTypeFont::parse() {
    if (data_.size() < 12) return makeError(ErrorCode::CorruptData, "font too small");
    const uint32_t version = u32(0);
    if (version != 0x00010000 && version != 0x74727565 /* 'true' */)
        return makeError(ErrorCode::Unsupported, "not a TrueType (glyf) font");
    const uint16_t numTables = u16(4);
    uint32_t head = 0, maxp = 0, hhea = 0;
    for (uint16_t i = 0; i < numTables; ++i) {
        const uint32_t rec = 12u + 16u * i;
        const uint32_t tag = u32(rec);
        const uint32_t off = u32(rec + 8);
        const uint32_t len = u32(rec + 12);
        if (static_cast<uint64_t>(off) + len > data_.size()) return makeError(ErrorCode::CorruptData, "font table out of range");
        switch (tag) {
        case 0x636D6170: cmap_ = off; break;              // cmap
        case 0x6C6F6361: loca_ = off; break;              // loca
        case 0x676C7966: glyf_ = off; glyfLen_ = len; break; // glyf
        case 0x686D7478: hmtx_ = off; break;              // hmtx
        case 0x68656164: head = off; break;               // head
        case 0x6D617870: maxp = off; break;               // maxp
        case 0x68686561: hhea = off; break;               // hhea
        default: break;
        }
    }
    if (!cmap_ || !loca_ || !glyf_ || !hmtx_ || !head || !maxp || !hhea)
        return makeError(ErrorCode::Unsupported, "font lacks required tables (CFF fonts are not supported)");

    unitsPerEm_ = u16(head + 18);
    indexToLocFormat_ = i16(head + 50);
    numGlyphs_ = u16(maxp + 4);
    ascender_ = i16(hhea + 4);
    descender_ = i16(hhea + 6);
    lineGap_ = i16(hhea + 8);
    numHMetrics_ = u16(hhea + 34);
    if (unitsPerEm_ <= 0 || numHMetrics_ == 0) return makeError(ErrorCode::CorruptData, "bad font header");

    // Prefer a full-Unicode subtable (3,10 / 0,4 format 12), then BMP (3,1 / 0,3 format 4).
    const uint16_t numSub = u16(cmap_ + 2);
    int bestScore = -1;
    for (uint16_t i = 0; i < numSub; ++i) {
        const uint32_t rec = cmap_ + 4 + 8u * i;
        const uint16_t platform = u16(rec), encoding = u16(rec + 2);
        const uint32_t sub = cmap_ + u32(rec + 4);
        const uint16_t format = u16(sub);
        int score = -1;
        if (format == 12 && ((platform == 3 && encoding == 10) || platform == 0)) score = 3;
        else if (format == 4 && ((platform == 3 && encoding == 1) || platform == 0)) score = 2;
        if (score > bestScore) {
            bestScore = score;
            cmapSub_ = sub;
            cmapFormat_ = format;
        }
    }
    if (bestScore < 0) return makeError(ErrorCode::Unsupported, "no Unicode cmap");
    return {};
}

int TrueTypeFont::glyphIndex(char32_t cp) const {
    const uint32_t c = static_cast<uint32_t>(cp);
    if (cmapFormat_ == 4) {
        if (c > 0xFFFF) return 0;
        const uint16_t segX2 = u16(cmapSub_ + 6);
        const uint32_t ends = cmapSub_ + 14;
        const uint32_t starts = ends + segX2 + 2;
        const uint32_t deltas = starts + segX2;
        const uint32_t ranges = deltas + segX2;
        // Binary search over segment end codes.
        uint32_t lo = 0, hi = segX2 / 2u;
        while (lo < hi) {
            const uint32_t mid = (lo + hi) / 2;
            if (u16(ends + 2 * mid) < c) lo = mid + 1;
            else hi = mid;
        }
        if (lo >= segX2 / 2u) return 0;
        const uint16_t start = u16(starts + 2 * lo);
        if (c < start) return 0;
        const uint16_t delta = u16(deltas + 2 * lo);
        const uint16_t rangeOff = u16(ranges + 2 * lo);
        if (rangeOff == 0) return static_cast<uint16_t>(c + delta);
        const uint32_t addr = ranges + 2 * lo + rangeOff + 2 * (c - start);
        const uint16_t g = u16(addr);
        return g ? static_cast<uint16_t>(g + delta) : 0;
    }
    if (cmapFormat_ == 12) {
        const uint32_t nGroups = u32(cmapSub_ + 12);
        uint32_t lo = 0, hi = nGroups;
        while (lo < hi) {
            const uint32_t mid = (lo + hi) / 2;
            const uint32_t g = cmapSub_ + 16 + 12 * mid;
            if (u32(g + 4) < c) lo = mid + 1;
            else hi = mid;
        }
        if (lo >= nGroups) return 0;
        const uint32_t g = cmapSub_ + 16 + 12 * lo;
        const uint32_t start = u32(g);
        if (c < start) return 0;
        return static_cast<int>(u32(g + 8) + (c - start));
    }
    return 0;
}

int TrueTypeFont::advanceWidth(int glyph) const {
    if (glyph < 0) return 0;
    const int i = glyph < numHMetrics_ ? glyph : numHMetrics_ - 1;
    return u16(hmtx_ + 4u * static_cast<uint32_t>(i));
}

uint32_t TrueTypeFont::glyphOffset(int glyph, uint32_t& length) const {
    length = 0;
    if (glyph < 0 || glyph >= numGlyphs_) return 0;
    const uint32_t g = static_cast<uint32_t>(glyph);
    uint32_t a, b;
    if (indexToLocFormat_ == 0) {
        a = 2u * u16(loca_ + 2 * g);
        b = 2u * u16(loca_ + 2 * g + 2);
    } else {
        a = u32(loca_ + 4 * g);
        b = u32(loca_ + 4 * g + 4);
    }
    if (b <= a || b > glyfLen_) return 0;
    length = b - a;
    return glyf_ + a;
}

bool TrueTypeFont::bounds(int glyph, int& xMin, int& yMin, int& xMax, int& yMax) const {
    uint32_t len;
    const uint32_t g = glyphOffset(glyph, len);
    if (!len) return false;
    xMin = i16(g + 2);
    yMin = i16(g + 4);
    xMax = i16(g + 6);
    yMax = i16(g + 8);
    return true;
}

bool TrueTypeFont::outline(int glyph, std::vector<Contour>& out) const {
    out.clear();
    return outlineImpl(glyph, out, 0) && !out.empty();
}

bool TrueTypeFont::outlineImpl(int glyph, std::vector<Contour>& out, int depth) const {
    if (depth > 8) return false;
    uint32_t len;
    const uint32_t g = glyphOffset(glyph, len);
    if (!len) return true;  // empty glyph
    const int16_t numContours = i16(g);

    if (numContours >= 0) {
        const uint32_t endPts = g + 10;
        const uint16_t numPoints = numContours ? static_cast<uint16_t>(u16(endPts + 2u * (numContours - 1)) + 1) : 0;
        const uint16_t instrLen = u16(endPts + 2u * numContours);
        uint32_t p = endPts + 2u * numContours + 2 + instrLen;

        std::vector<uint8_t> flags(numPoints);
        for (uint16_t i = 0; i < numPoints;) {
            const uint8_t f = u8(p++);
            flags[i++] = f;
            if (f & 8) {
                uint8_t repeat = u8(p++);
                while (repeat-- && i < numPoints) flags[i++] = f;
            }
        }
        std::vector<int> xs(numPoints), ys(numPoints);
        int v = 0;
        for (uint16_t i = 0; i < numPoints; ++i) {
            const uint8_t f = flags[i];
            if (f & 2) {
                const int d = u8(p++);
                v += (f & 16) ? d : -d;
            } else if (!(f & 16)) {
                v += i16(p);
                p += 2;
            }
            xs[i] = v;
        }
        v = 0;
        for (uint16_t i = 0; i < numPoints; ++i) {
            const uint8_t f = flags[i];
            if (f & 4) {
                const int d = u8(p++);
                v += (f & 32) ? d : -d;
            } else if (!(f & 32)) {
                v += i16(p);
                p += 2;
            }
            ys[i] = v;
        }
        uint16_t begin = 0;
        for (int c = 0; c < numContours; ++c) {
            const uint16_t end = u16(endPts + 2u * c);
            if (end < begin || end >= numPoints) return false;
            Contour contour;
            for (uint16_t i = begin; i <= end; ++i) {
                contour.points.push_back({static_cast<float>(xs[i]), static_cast<float>(ys[i])});
                contour.onCurve.push_back((flags[i] & 1) != 0);
            }
            out.push_back(std::move(contour));
            begin = static_cast<uint16_t>(end + 1);
        }
        return true;
    }

    // Composite glyph.
    uint32_t p = g + 10;
    for (;;) {
        const uint16_t flags = u16(p);
        const uint16_t sub = u16(p + 2);
        p += 4;
        float dx = 0, dy = 0;
        if (flags & 1) {  // ARG_1_AND_2_ARE_WORDS
            dx = i16(p);
            dy = i16(p + 2);
            p += 4;
        } else {
            dx = static_cast<int8_t>(u8(p));
            dy = static_cast<int8_t>(u8(p + 1));
            p += 2;
        }
        if (!(flags & 2)) dx = dy = 0;  // point matching is not supported
        float a = 1, b = 0, c = 0, d = 1;
        auto f2dot14 = [&](uint32_t off) { return static_cast<float>(i16(off)) / 16384.0f; };
        if (flags & 8) {
            a = d = f2dot14(p);
            p += 2;
        } else if (flags & 0x40) {
            a = f2dot14(p);
            d = f2dot14(p + 2);
            p += 4;
        } else if (flags & 0x80) {
            a = f2dot14(p);
            b = f2dot14(p + 2);
            c = f2dot14(p + 4);
            d = f2dot14(p + 6);
            p += 8;
        }
        std::vector<Contour> part;
        if (!outlineImpl(sub, part, depth + 1)) return false;
        for (Contour& ct : part) {
            for (PointF& pt : ct.points) {
                const float x = pt.x, y = pt.y;
                pt.x = a * x + c * y + dx;
                pt.y = b * x + d * y + dy;
            }
            out.push_back(std::move(ct));
        }
        if (!(flags & 0x20)) break;  // MORE_COMPONENTS
    }
    return true;
}

} // namespace pchess::ui
