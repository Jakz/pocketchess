// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"
#include "ui/canvas/Rasterizer.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace pchess::ui {

// Minimal TrueType (glyf) reader: cmap formats 4 and 12, simple and composite glyphs (offset
// transforms), horizontal metrics. Enough for Latin UI text and the chess glyphs; no hinting,
// no shaping. Self-contained so the canvas stays deterministic and dependency-free (ADR 0005).
class TrueTypeFont {
public:
    static Result<std::shared_ptr<TrueTypeFont>> load(const std::filesystem::path& file);
    static Result<std::shared_ptr<TrueTypeFont>> fromBytes(std::vector<uint8_t> bytes);

    int unitsPerEm() const { return unitsPerEm_; }
    int ascender() const { return ascender_; }
    int descender() const { return descender_; }  // negative
    int lineGap() const { return lineGap_; }
    int numGlyphs() const { return numGlyphs_; }

    int glyphIndex(char32_t codepoint) const;  // 0 = missing glyph
    int advanceWidth(int glyph) const;
    // Outline in font units (y up). Returns false for an empty glyph (e.g. space).
    bool outline(int glyph, std::vector<Contour>& out) const;
    // Bounding box of the glyph in font units: xMin, yMin, xMax, yMax.
    bool bounds(int glyph, int& xMin, int& yMin, int& xMax, int& yMax) const;

private:
    TrueTypeFont() = default;
    Result<void> parse();
    uint32_t glyphOffset(int glyph, uint32_t& length) const;
    bool outlineImpl(int glyph, std::vector<Contour>& out, int depth) const;

    uint8_t u8(uint32_t off) const { return off < data_.size() ? data_[off] : 0; }
    uint16_t u16(uint32_t off) const { return static_cast<uint16_t>((u8(off) << 8) | u8(off + 1)); }
    int16_t i16(uint32_t off) const { return static_cast<int16_t>(u16(off)); }
    uint32_t u32(uint32_t off) const { return (uint32_t{u16(off)} << 16) | u16(off + 2); }

    std::vector<uint8_t> data_;
    uint32_t cmap_ = 0, loca_ = 0, glyf_ = 0, hmtx_ = 0, glyfLen_ = 0;
    uint32_t cmapSub_ = 0;
    uint16_t cmapFormat_ = 0;
    int unitsPerEm_ = 1000, ascender_ = 0, descender_ = 0, lineGap_ = 0;
    int numGlyphs_ = 0, numHMetrics_ = 0, indexToLocFormat_ = 0;
};

} // namespace pchess::ui
