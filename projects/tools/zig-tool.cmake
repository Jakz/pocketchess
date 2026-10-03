# SPDX-License-Identifier: GPL-3.0-or-later
# Toolchain for the desktop tools (projects/tools): Zig as C/C++ compiler for the target in
# ZIG_TARGET (x86_64-linux-musl, aarch64-macos, x86_64-macos, x86_64-windows-gnu). ZIG_EXE and
# ZIG_TARGET are set by build-tools.ps1; the .cmd wrappers pass them to zig.
if(NOT DEFINED ENV{ZIG_EXE} OR NOT DEFINED ENV{ZIG_TARGET})
    message(FATAL_ERROR "ZIG_EXE / ZIG_TARGET are not set (run projects/tools/build-tools.ps1)")
endif()
if($ENV{ZIG_TARGET} MATCHES "linux")
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")  # musl: runs on any distribution
elseif($ENV{ZIG_TARGET} MATCHES "macos")
    set(CMAKE_SYSTEM_NAME Darwin)
else()
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
endif()
if($ENV{ZIG_TARGET} MATCHES "^aarch64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "Zig does not ship clang-scan-deps" FORCE)
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-tool-cc.cmd")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-tool-cxx.cmd")
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-tool-ar.cmd")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-tool-ranlib.cmd")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
