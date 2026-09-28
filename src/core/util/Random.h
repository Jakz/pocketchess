// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace pchess::util {

// Seedable PRNG (xoshiro256**, seeded by splitmix64). Every random source goes through this.
class Random {
public:
    explicit Random(uint64_t seed = 0x9E3779B97F4A7C15ULL) { reseed(seed); }

    void reseed(uint64_t seed) {
        for (auto& s : s_) s = splitmix64(seed);
    }

    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform in [0, bound) without modulo bias.
    uint32_t below(uint32_t bound) {
        if (bound == 0) return 0;
        const uint64_t threshold = (0x1'0000'0000ULL - bound) % bound;
        for (;;) {
            const uint64_t r = next() >> 32;
            if (r >= threshold) return static_cast<uint32_t>(r % bound);
        }
    }

    static uint64_t splitmix64(uint64_t& state) {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4]{};
};

} // namespace pchess::util
