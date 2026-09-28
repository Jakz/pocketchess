// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Canvas.h"
#include "ui/canvas/TrueTypeFont.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pchess::ui {

struct Glyph {
    AlphaMask mask;  // left/top relative to the pen position on the baseline
    int advance = 0;
};

// A face at one pixel size (em = px) with a glyph cache. Glyphs are placed on whole pixels.
class Font {
public:
    Font(std::shared_ptr<const TrueTypeFont> face, int px);

    int px() const { return px_; }
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }  // positive, below baseline
    int lineHeight() const { return ascent_ + descent_ + lineGap_; }

    const Glyph& glyph(char32_t cp);
    int measure(std::string_view utf8);
    // Draws with the baseline at y. Returns the pen x after the text.
    int draw(Canvas& canvas, int x, int baseline, std::string_view utf8, Rgb color);
    // Draws a single line centered in r (vertically by ascent/descent).
    void drawCentered(Canvas& canvas, const Rect& r, std::string_view utf8, Rgb color);
    // Greedy word wrap into lines no wider than maxWidth.
    std::vector<std::string> wrap(std::string_view utf8, int maxWidth);

private:
    std::shared_ptr<const TrueTypeFont> face_;
    int px_;
    float scale_;
    int ascent_, descent_, lineGap_;
    std::unordered_map<char32_t, Glyph> cache_;
};

enum class FontStyle { Regular, Bold };

// Owns the faces (Regular/Bold) and the sized fonts.
class FontLibrary {
public:
    Result<void> load(const std::filesystem::path& regular, const std::filesystem::path& bold);
    void setFaces(std::shared_ptr<const TrueTypeFont> regular, std::shared_ptr<const TrueTypeFont> bold);
    bool loaded() const { return regular_ != nullptr; }
    Font& get(int px, FontStyle style = FontStyle::Regular);
    std::shared_ptr<const TrueTypeFont> face(FontStyle style = FontStyle::Regular) const {
        return style == FontStyle::Bold && bold_ ? bold_ : regular_;
    }

private:
    std::shared_ptr<const TrueTypeFont> regular_, bold_;
    std::map<std::pair<int, int>, std::unique_ptr<Font>> fonts_;
};

} // namespace pchess::ui
