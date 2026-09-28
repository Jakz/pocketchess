// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace pchess::test {

// Scripted UCI engine on stdin/stdout (`--fake-engine`), used by the process/probe tests on
// desktop and in the device self-test.
int fakeEngine();

// Bulk-counted perft micro-benchmark (`--bench-movegen`, architecture doc §7).
int benchMovegen();

} // namespace pchess::test
