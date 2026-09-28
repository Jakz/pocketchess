// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/canvas/PngWriter.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace pchess::ui {
namespace {

class BitWriter {
public:
    void bits(uint32_t value, int count) {  // LSB-first
        acc_ |= static_cast<uint64_t>(value) << n_;
        n_ += count;
        while (n_ >= 8) {
            out.push_back(static_cast<uint8_t>(acc_));
            acc_ >>= 8;
            n_ -= 8;
        }
    }
    void huff(uint32_t code, int len) {  // Huffman codes are MSB-first
        uint32_t rev = 0;
        for (int i = 0; i < len; ++i) rev |= ((code >> i) & 1u) << (len - 1 - i);
        bits(rev, len);
    }
    void flush() {
        if (n_ > 0) out.push_back(static_cast<uint8_t>(acc_));
        acc_ = 0;
        n_ = 0;
    }
    std::vector<uint8_t> out;

private:
    uint64_t acc_ = 0;
    int n_ = 0;
};

constexpr uint16_t LenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t LenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t DistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t DistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void writeLiteral(BitWriter& w, uint32_t sym) {
    if (sym < 144) w.huff(0x30 + sym, 8);
    else if (sym < 256) w.huff(0x190 + (sym - 144), 9);
    else if (sym < 280) w.huff(sym - 256, 7);
    else w.huff(0xC0 + (sym - 280), 8);
}

void writeMatch(BitWriter& w, int len, int dist) {
    int li = 28;
    while (LenBase[li] > len) --li;
    writeLiteral(w, 257u + static_cast<uint32_t>(li));
    if (LenExtra[li]) w.bits(static_cast<uint32_t>(len - LenBase[li]), LenExtra[li]);
    int di = 29;
    while (DistBase[di] > dist) --di;
    w.huff(static_cast<uint32_t>(di), 5);
    if (DistExtra[di]) w.bits(static_cast<uint32_t>(dist - DistBase[di]), DistExtra[di]);
}

std::vector<uint8_t> deflateFixed(const std::vector<uint8_t>& in) {
    constexpr int Window = 32768, HashBits = 15, MaxChain = 48, MaxLen = 258;
    std::vector<int32_t> head(1u << HashBits, -1), prev(in.size(), -1);
    auto hash = [&](size_t i) {
        return ((uint32_t{in[i]} << 10) ^ (uint32_t{in[i + 1]} << 5) ^ in[i + 2]) & ((1u << HashBits) - 1);
    };
    BitWriter w;
    w.bits(1, 1);  // BFINAL
    w.bits(1, 2);  // BTYPE = fixed Huffman
    size_t i = 0;
    const size_t n = in.size();
    auto insert = [&](size_t pos) {
        if (pos + 2 >= n) return;
        const uint32_t h = hash(pos);
        prev[pos] = head[h];
        head[h] = static_cast<int32_t>(pos);
    };
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            int32_t cand = head[hash(i)];
            int chain = 0;
            const size_t maxLen = std::min<size_t>(MaxLen, n - i);
            while (cand >= 0 && chain++ < MaxChain && static_cast<int>(i - static_cast<size_t>(cand)) <= Window) {
                const size_t c = static_cast<size_t>(cand);
                size_t l = 0;
                while (l < maxLen && in[c + l] == in[i + l]) ++l;
                if (static_cast<int>(l) > bestLen) {
                    bestLen = static_cast<int>(l);
                    bestDist = static_cast<int>(i - c);
                    if (l == maxLen) break;
                }
                cand = prev[c];
            }
        }
        if (bestLen >= 3) {
            writeMatch(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + static_cast<size_t>(k));
            i += static_cast<size_t>(bestLen);
        } else {
            writeLiteral(w, in[i]);
            insert(i);
            ++i;
        }
    }
    writeLiteral(w, 256);
    w.flush();
    return std::move(w.out);
}

uint32_t adler32(const std::vector<uint8_t>& d) {
    uint32_t a = 1, b = 0;
    for (uint8_t c : d) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    putU32(png, static_cast<uint32_t>(data.size()));
    const size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    putU32(png, crc32(png.data() + start, png.size() - start));
}

std::vector<uint8_t> encodeRaw(const std::vector<uint8_t>& filtered, int width, int height, uint8_t colorType) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    putU32(ihdr, static_cast<uint32_t>(width));
    putU32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, colorType, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    std::vector<uint8_t> z = {0x78, 0x01};
    const auto def = deflateFixed(filtered);
    z.insert(z.end(), def.begin(), def.end());
    putU32(z, adler32(filtered));
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

// Chooses None/Sub/Up per row by the minimum-sum-of-absolute-differences heuristic.
std::vector<uint8_t> filterRows(const uint8_t* px, int width, int height, int stride, int bpp) {
    const size_t rowBytes = static_cast<size_t>(width) * static_cast<size_t>(bpp);
    std::vector<uint8_t> out;
    out.reserve((rowBytes + 1) * static_cast<size_t>(height));
    std::vector<uint8_t> cand[3];
    for (auto& c : cand) c.resize(rowBytes);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = px + static_cast<size_t>(y) * static_cast<size_t>(stride);
        const uint8_t* up = y ? row - stride : nullptr;
        uint64_t score[3] = {0, 0, 0};
        for (size_t i = 0; i < rowBytes; ++i) {
            const uint8_t left = i >= static_cast<size_t>(bpp) ? row[i - static_cast<size_t>(bpp)] : 0;
            const uint8_t above = up ? up[i] : 0;
            cand[0][i] = row[i];
            cand[1][i] = static_cast<uint8_t>(row[i] - left);
            cand[2][i] = static_cast<uint8_t>(row[i] - above);
            for (int f = 0; f < 3; ++f) score[f] += static_cast<uint64_t>(std::abs(static_cast<int8_t>(cand[f][i])));
        }
        int best = 0;
        for (int f = 1; f < 3; ++f)
            if (score[f] < score[best]) best = f;
        out.push_back(static_cast<uint8_t>(best));
        out.insert(out.end(), cand[best].begin(), cand[best].end());
    }
    return out;
}

} // namespace

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::vector<uint8_t> encodePngRgb(const uint8_t* rgb, int width, int height, int stride) {
    return encodeRaw(filterRows(rgb, width, height, stride, 3), width, height, 2);
}

std::vector<uint8_t> encodePng(const Canvas& canvas, const Rect& area) {
    const Rect a = area.intersected(canvas.bounds());
    return encodePngRgb(canvas.row(a.y) + a.x * 3, a.w, a.h, canvas.stride());
}

std::vector<uint8_t> encodePng(const Bitmap& b) {
    return encodeRaw(filterRows(b.rgba.data(), b.width, b.height, b.width * 4, 4), b.width, b.height, 6);
}

Result<void> writeFile(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::FILE* f = std::fopen(file.string().c_str(), "wb");
    if (!f) return makeError(ErrorCode::Io, "cannot write " + file.string());
    const size_t n = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (n != bytes.size()) return makeError(ErrorCode::Io, "short write " + file.string());
    return {};
}

Result<void> writePng(const std::filesystem::path& file, const Canvas& canvas) {
    return writeFile(file, encodePng(canvas, canvas.bounds()));
}

} // namespace pchess::ui
