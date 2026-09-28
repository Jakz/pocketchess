// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/chess/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pchess::chess {

// 16-bit move: from(6) | to(6) << 6 | flags(4) << 12 (architecture doc §7).
// Castling is encoded as the king's two-square move (e1g1), like UCI.
// The in-memory value is also the MoveCodec storage format (little-endian uint16).
class Move {
public:
    enum Flag : uint16_t {
        Quiet = 0,
        DoublePush = 1,
        CastleKing = 2,
        CastleQueen = 3,
        Capture = 4,
        EnPassant = 5,
        PromoKnight = 8,
        PromoBishop = 9,
        PromoRook = 10,
        PromoQueen = 11,
        PromoKnightCapture = 12,
        PromoBishopCapture = 13,
        PromoRookCapture = 14,
        PromoQueenCapture = 15,
    };

    constexpr Move() = default;
    constexpr Move(Square from, Square to, uint16_t flags = Quiet)
        : v_(static_cast<uint16_t>((from & 63) | ((to & 63) << 6) | ((flags & 15) << 12))) {}

    static constexpr Move fromRaw(uint16_t raw) {
        Move m;
        m.v_ = raw;
        return m;
    }
    static constexpr Move none() { return Move(); }

    constexpr Square from() const { return v_ & 63; }
    constexpr Square to() const { return (v_ >> 6) & 63; }
    constexpr uint16_t flags() const { return static_cast<uint16_t>(v_ >> 12); }
    constexpr uint16_t raw() const { return v_; }

    constexpr bool isNone() const { return v_ == 0; }
    constexpr bool isCapture() const { return (flags() & 4) != 0; }
    constexpr bool isPromotion() const { return (flags() & 8) != 0; }
    constexpr bool isCastle() const { return flags() == CastleKing || flags() == CastleQueen; }
    constexpr bool isEnPassant() const { return flags() == EnPassant; }
    constexpr PieceType promotion() const {
        return isPromotion() ? static_cast<PieceType>(Knight + (flags() & 3)) : NoPieceType;
    }

    constexpr bool operator==(const Move&) const = default;

private:
    uint16_t v_ = 0;
};

constexpr uint16_t promoFlag(PieceType t, bool capture) {
    return static_cast<uint16_t>(8 + (t - Knight) + (capture ? 4 : 0));
}

// Fixed-capacity move list (no allocation in move generation).
class MoveList {
public:
    static constexpr size_t Capacity = 256;

    void push(Move m) { moves_[size_++] = m; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    Move operator[](size_t i) const { return moves_[i]; }
    const Move* begin() const { return moves_.data(); }
    const Move* end() const { return moves_.data() + size_; }
    bool contains(Move m) const {
        for (size_t i = 0; i < size_; ++i)
            if (moves_[i] == m) return true;
        return false;
    }
    void clear() { size_ = 0; }

private:
    std::array<Move, Capacity> moves_{};
    size_t size_ = 0;
};

} // namespace pchess::chess
