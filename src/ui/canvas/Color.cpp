// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/canvas/Color.h"

namespace pchess::ui {

std::optional<Rgb> parseColor(std::string_view t) {
    if (!t.empty() && t[0] == '#') t.remove_prefix(1);
    if (t.size() != 6) return std::nullopt;
    uint32_t v = 0;
    for (char c : t) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
        else return std::nullopt;
    }
    return Rgb::hex(v);
}

} // namespace pchess::ui
