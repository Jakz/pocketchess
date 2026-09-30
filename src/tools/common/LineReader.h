// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Desktop tools (architecture doc §12): line-by-line reading of a text file, decompressing a
// `.zst` file on the fly (no temporary file, bounded memory).

#include "core/util/Error.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace pchess::tools {

// Calls `onLine` for every line (without the newline). `onLine` returns false to stop early.
// Returns the number of lines delivered.
Result<uint64_t> forEachLine(const std::filesystem::path& file, const std::function<bool(std::string_view)>& onLine);

} // namespace pchess::tools
