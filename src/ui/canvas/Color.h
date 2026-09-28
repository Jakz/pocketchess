// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace pchess::ui {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    constexpr bool operator==(const Rgb&) const = default;
    static constexpr Rgb gray(uint8_t v) { return {v, v, v}; }
    static constexpr Rgb hex(uint32_t v) {
        return {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
    }
    // Rec. 601 luma in 0..255 (integer, deterministic).
    constexpr uint8_t luma() const { return static_cast<uint8_t>((299 * r + 587 * g + 114 * b + 500) / 1000); }
    constexpr Rgb toGray() const { return gray(luma()); }
};

namespace colors {
constexpr Rgb Black{0, 0, 0};
constexpr Rgb White{255, 255, 255};
constexpr Rgb Paper{250, 250, 247};
constexpr Rgb Ink{20, 20, 20};
constexpr Rgb MidGray{128, 128, 128};
} // namespace colors

// Integer blend: result = dst + (src - dst) * a / 255, rounded. Deterministic on every platform.
constexpr uint8_t blend8(uint8_t dst, uint8_t src, uint32_t a) {
    const uint32_t v = static_cast<uint32_t>(src) * a + static_cast<uint32_t>(dst) * (255 - a) + 127;
    return static_cast<uint8_t>((v + (v >> 8)) >> 8);
}
constexpr Rgb blend(Rgb dst, Rgb src, uint32_t a) {
    return {blend8(dst.r, src.r, a), blend8(dst.g, src.g, a), blend8(dst.b, src.b, a)};
}

// Parses "#rrggbb" or "rrggbb".
std::optional<Rgb> parseColor(std::string_view text);

} // namespace pchess::ui
