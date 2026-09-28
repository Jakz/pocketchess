// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/canvas/Font.h"

#include "core/util/Utf8.h"

#include <cmath>

namespace pchess::ui {

Font::Font(std::shared_ptr<const TrueTypeFont> face, int px) : face_(std::move(face)), px_(px) {
    if (!face_) {  // fonts missing: draw nothing rather than crash (design notes §32)
        scale_ = 0;
        ascent_ = px * 3 / 4;
        descent_ = px / 4;
        lineGap_ = 0;
        return;
    }
    scale_ = static_cast<float>(px) / static_cast<float>(face_->unitsPerEm());
    ascent_ = static_cast<int>(std::ceil(static_cast<float>(face_->ascender()) * scale_));
    descent_ = static_cast<int>(std::ceil(static_cast<float>(-face_->descender()) * scale_));
    lineGap_ = static_cast<int>(std::lround(static_cast<float>(face_->lineGap()) * scale_));
}

const Glyph& Font::glyph(char32_t cp) {
    auto it = cache_.find(cp);
    if (it != cache_.end()) return it->second;

    Glyph g;
    if (!face_) return cache_.emplace(cp, std::move(g)).first->second;
    int index = face_->glyphIndex(cp);
    if (index == 0 && cp != U'?') index = face_->glyphIndex(U'?');
    g.advance = static_cast<int>(std::lround(static_cast<float>(face_->advanceWidth(index)) * scale_));

    int x0, y0, x1, y1;
    std::vector<Contour> contours;
    if (face_->bounds(index, x0, y0, x1, y1) && face_->outline(index, contours)) {
        // Pixel box (y down): 1 px of padding for anti-aliasing.
        const int left = static_cast<int>(std::floor(static_cast<float>(x0) * scale_)) - 1;
        const int right = static_cast<int>(std::ceil(static_cast<float>(x1) * scale_)) + 1;
        const int top = static_cast<int>(std::floor(static_cast<float>(-y1) * scale_)) - 1;
        const int bottom = static_cast<int>(std::ceil(static_cast<float>(-y0) * scale_)) + 1;
        Rasterizer r(right - left, bottom - top);
        r.addContours(contours, scale_, -scale_, static_cast<float>(-left), static_cast<float>(-top));
        g.mask = r.finish();
        g.mask.left = left;
        g.mask.top = top;
    }
    return cache_.emplace(cp, std::move(g)).first->second;
}

int Font::measure(std::string_view text) {
    int w = 0;
    size_t pos = 0;
    while (pos < text.size()) w += glyph(util::decodeUtf8(text, pos)).advance;
    return w;
}

int Font::draw(Canvas& canvas, int x, int baseline, std::string_view text, Rgb color) {
    size_t pos = 0;
    while (pos < text.size()) {
        const Glyph& g = glyph(util::decodeUtf8(text, pos));
        if (g.mask.width) canvas.drawMask(g.mask, x, baseline, color);
        x += g.advance;
    }
    return x;
}

void Font::drawCentered(Canvas& canvas, const Rect& r, std::string_view text, Rgb color) {
    const int w = measure(text);
    const int baseline = r.y + (r.h - (ascent_ + descent_)) / 2 + ascent_;
    draw(canvas, r.x + (r.w - w) / 2, baseline, text, color);
}

std::vector<std::string> Font::wrap(std::string_view text, int maxWidth) {
    std::vector<std::string> lines;
    std::string line;
    int lineW = 0;
    const int spaceW = glyph(U' ').advance;
    size_t i = 0;
    while (i <= text.size()) {
        size_t j = text.find_first_of(" \n", i);
        if (j == std::string_view::npos) j = text.size();
        const std::string_view word = text.substr(i, j - i);
        const int ww = measure(word);
        if (!line.empty() && lineW + spaceW + ww > maxWidth) {
            lines.push_back(line);
            line.clear();
            lineW = 0;
        }
        if (!line.empty()) {
            line += ' ';
            lineW += spaceW;
        }
        line += word;
        lineW += ww;
        if (j < text.size() && text[j] == '\n') {
            lines.push_back(line);
            line.clear();
            lineW = 0;
        }
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

Result<void> FontLibrary::load(const std::filesystem::path& regular, const std::filesystem::path& bold) {
    auto r = TrueTypeFont::load(regular);
    if (!r) return r.error();
    regular_ = r.value();
    auto b = TrueTypeFont::load(bold);
    bold_ = b ? b.value() : regular_;
    fonts_.clear();
    return {};
}

void FontLibrary::setFaces(std::shared_ptr<const TrueTypeFont> regular, std::shared_ptr<const TrueTypeFont> bold) {
    regular_ = std::move(regular);
    bold_ = bold ? std::move(bold) : regular_;
    fonts_.clear();
}

Font& FontLibrary::get(int px, FontStyle style) {
    const std::pair<int, int> key{px, style == FontStyle::Bold ? 1 : 0};
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return *it->second;
    auto font = std::make_unique<Font>(face(style), px);
    return *fonts_.emplace(key, std::move(font)).first->second;
}

} // namespace pchess::ui
