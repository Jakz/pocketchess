// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"
#include "ui/theme/PieceSet.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct NSVGimage;

namespace pchess::ui {

// A piece set folder (design notes §18, ADR 0012): assets/pieces/<id>/
//
//   pieceset.yaml   name (required), author, license (required), source,
//                   scale (share of the square, 0.5..1.2, default 1),
//                   stroke_scale (multiplies every stroke width, 0.5..4, default 1),
//                   base (a sibling folder whose SVGs this set reuses, e.g. a bolder variant)
//   wK.svg wQ.svg wR.svg wB.svg wN.svg wP.svg bK.svg ... bP.svg
//
// SVGs are parsed once and rasterized at the exact square size on first use; PieceCache keeps the
// bitmaps (§18.1: never rescale on redraw).
struct PieceSetInfo {
    std::string id;  // folder name
    std::string name;
    std::string author;
    std::string license;
    std::string source;
    std::string base;
    double scale = 1.0;
    double strokeScale = 1.0;
    std::filesystem::path dir;
};

Result<PieceSetInfo> readPieceSetInfo(const std::filesystem::path& dir);

class SvgPieceSet final : public PieceSet {
public:
    // Every one of the 12 SVGs must parse, or the set is rejected (design notes §32: the caller
    // then keeps the glyph set).
    static Result<std::shared_ptr<SvgPieceSet>> load(const std::filesystem::path& dir);
    ~SvgPieceSet() override;
    SvgPieceSet(const SvgPieceSet&) = delete;
    SvgPieceSet& operator=(const SvgPieceSet&) = delete;

    std::string name() const override { return info_.name; }
    Bitmap render(chess::Piece piece, int squarePx) const override;
    const PieceSetInfo& info() const { return info_; }

private:
    SvgPieceSet() = default;
    PieceSetInfo info_;
    std::array<NSVGimage*, 12> images_{};
    float unit_ = 1;  // the largest image extent: one scale for the whole set
};

// The installed piece sets (assets/pieces/*/pieceset.yaml). Only the manifests are read here; a
// set's SVGs are parsed when it is chosen.
class PieceSetLibrary {
public:
    struct Problem {
        std::filesystem::path dir;
        std::string message;
    };

    int scan(const std::filesystem::path& root);
    const std::vector<PieceSetInfo>& sets() const { return sets_; }
    const PieceSetInfo* find(const std::string& name) const;  // by name or folder id
    const std::vector<Problem>& problems() const { return problems_; }

private:
    std::vector<PieceSetInfo> sets_;
    std::vector<Problem> problems_;
};

} // namespace pchess::ui
