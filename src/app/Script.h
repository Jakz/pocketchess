// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"

#include <functional>
#include <string>

namespace pchess::app {

class Application;

// Scripted input for screenshots, docs and UI tests (desktop `--script`, headless tool).
// Commands separated by ';':
//   tap:e2 | tap:540,700 | long:e2 | drag:e2-e4 | key:next|prev|menu|back|ok|home|up|down
//   text:<utf8> | wait:<ms>
// Square names map through the unflipped board layout.
Result<void> runScript(Application& app, const std::string& script, const std::function<void(int ms)>& wait = {});

} // namespace pchess::app
