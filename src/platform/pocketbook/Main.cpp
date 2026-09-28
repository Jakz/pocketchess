// SPDX-License-Identifier: GPL-3.0-or-later
// pocketchess.app: the device build (build doc §4).
#include "platform/pocketbook/InkViewPlatform.h"

int main(int argc, char** argv) { return pchess::platform::pocketbook::runInkViewApp(argc, argv); }
