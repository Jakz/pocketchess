// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace pchess::util {

// Decodes one code point starting at text[pos] and advances pos. Invalid sequences yield U+FFFD
// and advance by one byte. Safe with signed or unsigned char.
char32_t decodeUtf8(std::string_view text, size_t& pos);

void appendUtf8(std::string& out, char32_t cp);

std::string trim(std::string_view s);
std::string toLowerAscii(std::string_view s);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);

} // namespace pchess::util
