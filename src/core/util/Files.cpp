// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Files.h"

#include <fstream>

namespace pchess::util {

Result<std::string> readTextFile(const std::filesystem::path& file, uintmax_t maxBytes) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) return makeError(ErrorCode::NotFound, file.filename().string() + ": " + ec.message());
    if (size > maxBytes)
        return makeError(ErrorCode::Unsupported, file.filename().string() + ": " + std::to_string(size >> 20) +
                                                     " MB is larger than the " + std::to_string(maxBytes >> 20) +
                                                     " MB limit");
    std::ifstream in(file, std::ios::binary);
    if (!in) return makeError(ErrorCode::NotFound, file.filename().string());
    std::string text(static_cast<size_t>(size), '\0');
    if (size > 0 && !in.read(text.data(), static_cast<std::streamsize>(size)))
        return makeError(ErrorCode::Io, file.filename().string() + ": read failed");
    return text;
}

} // namespace pchess::util
