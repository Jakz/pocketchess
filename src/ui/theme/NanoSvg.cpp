// SPDX-License-Identifier: GPL-3.0-or-later
// The one translation unit that compiles the vendored nanosvg parser and rasterizer (libs/nanosvg,
// zlib licence; ADR 0012). Third-party code: warnings off.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#pragma warning(disable : 4244 4456 4701 4702)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
