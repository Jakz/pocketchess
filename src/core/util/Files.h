// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace pchess::util {

// Whole file as bytes (no newline translation). NotFound if it cannot be opened, Unsupported if
// it is larger than `maxBytes` (imports must not exhaust the reader's RAM), Io on read errors.
Result<std::string> readTextFile(const std::filesystem::path& file, uintmax_t maxBytes = 64u << 20);

} // namespace pchess::util
