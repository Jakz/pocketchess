// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/canvas/Canvas.h"

#include <string>

namespace pchess::test {

// Golden images (testing doc §1): the canvas is deterministic, so screens are compared by an
// exact 64-bit hash stored in <goldenDir>/golden.txt; the PNG next to it is for humans.
// `--update-golden` rewrites both. On a mismatch the actual image is written to
// <goldenDir>/actual/<name>.png for inspection.
void checkGolden(const std::string& name, const ui::Canvas& canvas);

} // namespace pchess::test
