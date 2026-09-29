# SPDX-License-Identifier: GPL-3.0-or-later
# Toolchain for Windows x64 engine builds with Zig (build doc §4.3): same compiler family as
# the ARM build, so the engine sources are exercised identically on both targets. Like the
# shared ZigToolchain.cmake, the compilers are small .cmd wrappers that call %ZIG_EXE%.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(NOT DEFINED ENV{ZIG_EXE})
    message(FATAL_ERROR "ZIG_EXE is not set (run projects/pocketbook/build-engine.ps1)")
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "Zig does not ship clang-scan-deps" FORCE)
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-win-cc.cmd")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-win-cxx.cmd")
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-win-ar.cmd")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-win-ranlib.cmd")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
