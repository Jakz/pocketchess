# SPDX-License-Identifier: GPL-3.0-or-later
# Toolchain for device engine builds with Zig (build doc §4.3). Engines are separate processes,
# so they need not share the app's ABI: Zig's arm-linux-gnueabi target is soft-float (no NEON
# registers; LLVM aborts on Stockfish's NEON code), while a fully static arm-linux-musleabihf
# binary uses VFPv4/NEON and runs on the reader regardless of its glibc (ADR 0009).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7)
if(NOT DEFINED ENV{ZIG_EXE})
    message(FATAL_ERROR "ZIG_EXE is not set (run projects/pocketbook/build-engine.ps1)")
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "Zig does not ship clang-scan-deps" FORCE)
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-arm-cc.cmd")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-arm-cxx.cmd")
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-win-ar.cmd")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-win-ranlib.cmd")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_SKIP_BUILD_RPATH ON)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
