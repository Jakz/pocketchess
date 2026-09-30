// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Types.h"
#include "ui/canvas/Canvas.h"
#include "ui/canvas/TrueTypeFont.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace pchess::ui {

// Source of piece bitmaps for a given square size (design notes §18). Bitmaps are rendered once
// per size and cached (§18.1: never rescale on redraw).
class PieceSet {
public:
    virtual ~PieceSet() = default;
    virtual std::string name() const = 0;
    virtual Bitmap render(chess::Piece piece, int squarePx) const = 0;
};

// Built-in set drawn from the Unicode chess glyphs of a bundled font (DejaVu Sans): outline
// glyphs with the enclosed area flood-filled white. Serves until the PNG sets of M6 arrive, and
// stays as the always-available fallback when a set is missing or incomplete (design notes §32).
class GlyphPieceSet final : public PieceSet {
public:
    GlyphPieceSet(std::shared_ptr<const TrueTypeFont> face, std::string name = "Glyph");
    std::string name() const override { return name_; }
    Bitmap render(chess::Piece piece, int squarePx) const override;
    bool complete() const;  // all 12 glyphs present in the font

private:
    std::shared_ptr<const TrueTypeFont> face_;
    std::string name_;
};

// Caches rendered pieces per (piece, size).
class PieceCache {
public:
    explicit PieceCache(std::shared_ptr<const PieceSet> set = nullptr) : set_(std::move(set)) {}
    void setPieceSet(std::shared_ptr<const PieceSet> set) {
        set_ = std::move(set);
        cache_.clear();
    }
    const PieceSet* pieceSet() const { return set_.get(); }
    // Piece colors (BoardTheme::tintPieces): white pieces `white` with an `outline`, black pieces
    // `black` with `white` details. nullopt = the set's own colors.
    struct Tint {
        Rgb outline, white, black;
        bool operator==(const Tint&) const = default;
    };
    void setTint(std::optional<Tint> tint) {
        if (tint == tint_) return;
        tint_ = tint;
        cache_.clear();
        have_.clear();
    }
    const Bitmap& get(chess::Piece piece, int squarePx);
    size_t bytes() const;

private:
    std::shared_ptr<const PieceSet> set_;
    std::optional<Tint> tint_;
    std::map<int, std::array<Bitmap, 12>> cache_;
    std::map<int, std::array<bool, 12>> have_;
};

} // namespace pchess::ui
