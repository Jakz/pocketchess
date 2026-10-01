// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/chess/FenCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace pchess::chess {

namespace {

constexpr std::string_view kPieces = "PNBRQKpnbrqk";

Error bad(const std::string& what) { return makeError(ErrorCode::ParseFen, what); }

std::vector<std::string_view> fields(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        const size_t j = s.find(' ', i);
        if (i < s.size()) out.push_back(s.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i));
        if (j == std::string_view::npos) break;
        i = j;
    }
    return out;
}

int toInt(std::string_view s) {
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
    }
    return s.empty() ? -1 : v;
}

} // namespace

Result<std::string> packFen(std::string_view fen) {
    const auto f = fields(fen);
    if (f.size() < 4) return bad("packFen: too few fields");
    uint64_t occupied = 0;
    std::vector<uint8_t> nibbles;
    int sq = 0;
    for (char c : f[0]) {
        if (c == '/') continue;
        if (c >= '1' && c <= '8') {
            sq += c - '0';
            continue;
        }
        const size_t p = kPieces.find(c);
        if (p == std::string_view::npos || sq >= 64) return bad("packFen: bad board");
        occupied |= uint64_t(1) << sq;
        nibbles.push_back(static_cast<uint8_t>(p));
        ++sq;
    }
    if (sq != 64) return bad("packFen: board does not have 64 squares");
    std::string out;
    for (int i = 0; i < 8; ++i) out += static_cast<char>((occupied >> (8 * i)) & 0xFF);
    for (size_t i = 0; i < nibbles.size(); i += 2)
        out += static_cast<char>(nibbles[i] | (i + 1 < nibbles.size() ? nibbles[i + 1] << 4 : 0));
    uint8_t flags = f[1] == "b" ? 1 : 0;
    for (char c : f[2]) {
        if (c == 'K') flags |= 2;
        else if (c == 'Q') flags |= 4;
        else if (c == 'k') flags |= 8;
        else if (c == 'q') flags |= 16;
        else if (c != '-') return bad("packFen: castling field must be KQkq");
    }
    out += static_cast<char>(flags);
    out += static_cast<char>(f[3] == "-" || f[3].empty() ? 0 : f[3][0] - 'a' + 1);
    const int half = f.size() > 4 ? toInt(f[4]) : 0, full = f.size() > 5 ? toInt(f[5]) : 1;
    out += static_cast<char>(std::clamp(half, 0, 255));
    const int fm = std::clamp(full, 1, 65535);
    out += static_cast<char>(fm & 0xFF);
    out += static_cast<char>(fm >> 8);
    return out;
}

Result<std::string> unpackFen(std::string_view packed) {
    if (packed.size() < 13) return bad("unpackFen: too short");
    uint64_t occupied = 0;
    for (int i = 0; i < 8; ++i) occupied |= uint64_t(static_cast<unsigned char>(packed[static_cast<size_t>(i)])) << (8 * i);
    int count = 0;
    for (uint64_t o = occupied; o; o &= o - 1) ++count;
    const size_t nibbleBytes = static_cast<size_t>((count + 1) / 2);
    if (packed.size() != 8 + nibbleBytes + 5) return bad("unpackFen: bad length");
    std::string board;
    int empty = 0, k = 0;
    for (int sq = 0; sq < 64; ++sq) {
        if (sq > 0 && sq % 8 == 0) {
            if (empty) board += static_cast<char>('0' + empty);
            empty = 0;
            board += '/';
        }
        if (!((occupied >> sq) & 1)) {
            ++empty;
            continue;
        }
        if (empty) board += static_cast<char>('0' + empty);
        empty = 0;
        const unsigned char byte = static_cast<unsigned char>(packed[8 + static_cast<size_t>(k / 2)]);
        const int p = (k % 2 == 0) ? (byte & 0x0F) : (byte >> 4);
        if (p >= 12) return bad("unpackFen: bad piece");
        board += kPieces[static_cast<size_t>(p)];
        ++k;
    }
    if (empty) board += static_cast<char>('0' + empty);
    const size_t t = 8 + nibbleBytes;
    const unsigned char flags = static_cast<unsigned char>(packed[t]);
    std::string castling;
    if (flags & 2) castling += 'K';
    if (flags & 4) castling += 'Q';
    if (flags & 8) castling += 'k';
    if (flags & 16) castling += 'q';
    if (castling.empty()) castling = "-";
    const bool black = flags & 1;
    const int epFile = static_cast<unsigned char>(packed[t + 1]);
    std::string ep = "-";
    if (epFile > 0 && epFile <= 8) {
        ep = std::string(1, static_cast<char>('a' + epFile - 1));
        ep += black ? '3' : '6';  // the square behind the pawn that just moved two
    }
    const int half = static_cast<unsigned char>(packed[t + 2]);
    const int full = static_cast<unsigned char>(packed[t + 3]) | (static_cast<unsigned char>(packed[t + 4]) << 8);
    return board + (black ? " b " : " w ") + castling + " " + ep + " " + std::to_string(half) + " " +
           std::to_string(full);
}

} // namespace pchess::chess
