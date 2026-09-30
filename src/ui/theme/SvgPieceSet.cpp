// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme/SvgPieceSet.h"

#include "core/util/Files.h"
#include "core/util/Log.h"
#include "core/util/Yaml.h"

#include <algorithm>
#include <cmath>

#include "nanosvg.h"
#include "nanosvgrast.h"

namespace pchess::ui {

namespace {

// Lichess file names: {w,b}{K,Q,R,B,N,P}.svg, indexed like chess::Piece (Pawn..King per color).
std::string fileFor(int piece) {
    static const char types[6] = {'P', 'N', 'B', 'R', 'Q', 'K'};
    return std::string(1, piece < 6 ? 'w' : 'b') + types[piece % 6] + ".svg";
}

Error bad(const std::filesystem::path& dir, const std::string& what) {
    return makeError(ErrorCode::CorruptData, dir.filename().string() + ": " + what);
}

} // namespace

Result<PieceSetInfo> readPieceSetInfo(const std::filesystem::path& dir) {
    auto doc = util::loadYamlFile(dir / "pieceset.yaml");
    if (!doc) return bad(dir, "pieceset.yaml: " + doc.error().context);
    PieceSetInfo info;
    info.id = dir.filename().string();
    info.dir = dir;
    for (const auto& [key, value] : doc->entries()) {
        if (key == "name") info.name = value.stringOr("");
        else if (key == "author") info.author = value.stringOr("");
        else if (key == "license") info.license = value.stringOr("");
        else if (key == "source") info.source = value.stringOr("");
        else if (key == "base") info.base = value.stringOr("");
        else if (key == "scale" || key == "stroke_scale") {
            const bool isScale = key == "scale";
            const auto v = value.asDouble();
            if (!v || *v < 0.5 || *v > (isScale ? 1.2 : 4.0))
                return bad(dir, "line " + std::to_string(value.line()) + ": " + key + " must be a number from 0.5 to " +
                                    (isScale ? "1.2" : "4"));
            (isScale ? info.scale : info.strokeScale) = *v;
        } else {
            return bad(dir, "line " + std::to_string(value.line()) + ": unknown key '" + key + "'");
        }
    }
    if (info.name.empty()) return bad(dir, "pieceset.yaml: name missing");
    if (info.license.empty()) return bad(dir, "pieceset.yaml: license missing (design notes §18)");
    if (!info.base.empty() && (info.base.find_first_of("/\\") != std::string::npos || info.base == ".."))
        return bad(dir, "base must be a sibling folder name");
    return info;
}

Result<std::shared_ptr<SvgPieceSet>> SvgPieceSet::load(const std::filesystem::path& dir) {
    auto info = readPieceSetInfo(dir);
    if (!info) return info.error();
    std::shared_ptr<SvgPieceSet> set(new SvgPieceSet());
    set->info_ = std::move(*info);
    const std::filesystem::path svgDir = set->info_.base.empty() ? dir : dir.parent_path() / set->info_.base;
    for (int p = 0; p < 12; ++p) {
        const std::filesystem::path file = svgDir / fileFor(p);
        auto text = util::readTextFile(file, 1u << 20);
        if (!text) return bad(dir, fileFor(p) + ": " + text.error().context);
        NSVGimage* image = nsvgParse(text->data(), "px", 96.0f);  // parses in place (text is a copy)
        if (!image || !image->shapes || image->width <= 0 || image->height <= 0) {
            if (image) nsvgDelete(image);
            return bad(dir, fileFor(p) + ": not a drawable SVG");
        }
        if (set->info_.strokeScale != 1.0)
            for (NSVGshape* s = image->shapes; s; s = s->next)
                s->strokeWidth *= static_cast<float>(set->info_.strokeScale);
        set->unit_ = std::max({set->unit_, image->width, image->height});
        set->images_[static_cast<size_t>(p)] = image;
    }
    return set;
}

SvgPieceSet::~SvgPieceSet() {
    for (NSVGimage* image : images_)
        if (image) nsvgDelete(image);
}

Bitmap SvgPieceSet::render(chess::Piece piece, int px) const {
    Bitmap out(px, px);
    const int i = static_cast<int>(piece);
    if (i < 0 || i >= 12 || px <= 0) return out;
    NSVGimage* image = images_[static_cast<size_t>(i)];
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) return out;
    const float s = static_cast<float>(px) * static_cast<float>(info_.scale) / unit_;
    const float tx = (static_cast<float>(px) - image->width * s) / 2;
    const float ty = (static_cast<float>(px) - image->height * s) / 2;
    nsvgRasterize(rast, image, tx, ty, s, out.rgba.data(), px, px, px * 4);
    nsvgDeleteRasterizer(rast);
    return out;
}

int PieceSetLibrary::scan(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return 0;
    std::vector<std::filesystem::path> dirs;
    for (const auto& e : std::filesystem::directory_iterator(root, ec))
        if (e.is_directory(ec) && std::filesystem::exists(e.path() / "pieceset.yaml", ec)) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    int added = 0;
    for (const auto& d : dirs) {
        auto info = readPieceSetInfo(d);
        if (!info) {
            problems_.push_back({d, info.error().message()});
            util::Log::warn("pieces", info.error().message() + " - skipped");
            continue;
        }
        if (find(info->name)) {
            problems_.push_back({d, "a piece set named '" + info->name + "' already exists"});
            continue;
        }
        sets_.push_back(std::move(*info));
        ++added;
    }
    util::Log::info("pieces", std::to_string(added) + " piece set(s) in " + root.string());
    return added;
}

const PieceSetInfo* PieceSetLibrary::find(const std::string& name) const {
    for (const PieceSetInfo& s : sets_)
        if (s.name == name || s.id == name) return &s;
    return nullptr;
}

} // namespace pchess::ui
