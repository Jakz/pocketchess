// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Utf8.h"

namespace pchess::util {

char32_t decodeUtf8(std::string_view text, size_t& pos) {
    auto byte = [&](size_t i) -> uint32_t { return static_cast<unsigned char>(text[i]); };
    const size_t n = text.size();
    if (pos >= n) return 0;
    uint32_t b0 = byte(pos);
    if (b0 < 0x80) {
        ++pos;
        return b0;
    }
    int len = 0;
    uint32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; }
    else { ++pos; return 0xFFFD; }
    if (pos + static_cast<size_t>(len) > n) { ++pos; return 0xFFFD; }
    for (int i = 1; i < len; ++i) {
        uint32_t b = byte(pos + static_cast<size_t>(i));
        if ((b & 0xC0) != 0x80) { ++pos; return 0xFFFD; }
        cp = (cp << 6) | (b & 0x3F);
    }
    // Reject overlong encodings and surrogates.
    static const uint32_t minForLen[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < minForLen[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++pos; return 0xFFFD; }
    pos += static_cast<size_t>(len);
    return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
    auto put = [&](uint32_t v) { out.push_back(static_cast<char>(static_cast<unsigned char>(v))); };
    uint32_t c = cp;
    if (c < 0x80) put(c);
    else if (c < 0x800) { put(0xC0 | (c >> 6)); put(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { put(0xE0 | (c >> 12)); put(0x80 | ((c >> 6) & 0x3F)); put(0x80 | (c & 0x3F)); }
    else { put(0xF0 | (c >> 18)); put(0x80 | ((c >> 12) & 0x3F)); put(0x80 | ((c >> 6) & 0x3F)); put(0x80 | (c & 0x3F)); }
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

std::string toLowerAscii(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }
bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace pchess::util
