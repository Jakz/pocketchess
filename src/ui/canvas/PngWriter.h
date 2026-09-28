// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"
#include "ui/canvas/Canvas.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace pchess::ui {

// Small self-contained PNG encoder (fixed-Huffman deflate with LZ77), used for --screenshot,
// golden images and bug reports. Works on the device too (no zlib dependency).
std::vector<uint8_t> encodePngRgb(const uint8_t* rgb, int width, int height, int stride);
std::vector<uint8_t> encodePng(const Canvas& canvas, const Rect& area);
std::vector<uint8_t> encodePng(const Bitmap& rgba);
Result<void> writePng(const std::filesystem::path& file, const Canvas& canvas);
Result<void> writeFile(const std::filesystem::path& file, const std::vector<uint8_t>& bytes);

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

} // namespace pchess::ui
