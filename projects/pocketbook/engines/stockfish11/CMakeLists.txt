# SPDX-License-Identifier: GPL-3.0-or-later
# Stockfish 11 Eco engine candidate (M2, ADR 0004). Sources: libs/stockfish11/ (local only,
# fetch-engines.ps1). Classical evaluation, no networks. The flags replicate the upstream
# Makefile for ARCH=armv7 and x86-64-modern; BUILDING.md records where each flag comes from.
cmake_minimum_required(VERSION 3.20)
project(pchess_stockfish11 CXX)

set(ENGINE_SOURCE_DIR "" CACHE PATH "libs/stockfish11")
set(PCHESS_ENGINE_TARGET "arm" CACHE STRING "arm | windows-x64")
if(NOT EXISTS "${ENGINE_SOURCE_DIR}/src/uci.cpp")
    message(FATAL_ERROR "Stockfish 11 sources not found in '${ENGINE_SOURCE_DIR}' (run scripts/fetch-engines.ps1)")
endif()
set(SF "${ENGINE_SOURCE_DIR}/src")

file(GLOB SF_SOURCES "${SF}/*.cpp" "${SF}/syzygy/*.cpp")
add_executable(stockfish11 ${SF_SOURCES})
set_target_properties(stockfish11 PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(stockfish11 PRIVATE "${SF}")
target_compile_definitions(stockfish11 PRIVATE NDEBUG)
# misc.cpp always expands __DATE__ (engine_info); Zig's clang makes that an error by default.
target_compile_options(stockfish11 PRIVATE -O3 -fno-exceptions -Wno-date-time)
target_link_options(stockfish11 PRIVATE -s)  # no debug info in the shipped binary

if(PCHESS_ENGINE_TARGET STREQUAL "arm")
    # Makefile ARCH=armv7: bits=32, prefetch=yes, popcnt=no. zig-arm-engine.cmake sets the CPU.
else()
    # Makefile ARCH=x86-64-modern: bits=64, prefetch=yes, popcnt=yes (-msse3 -mpopcnt).
    target_compile_definitions(stockfish11 PRIVATE IS_64BIT USE_POPCNT)
    target_compile_options(stockfish11 PRIVATE -msse -msse3 -mpopcnt)
endif()

# thread_win32_osx.h uses pthreads on mingw (Makefile: -lpthread on Linux).
target_link_libraries(stockfish11 PRIVATE pthread)
if(WIN32)
    target_link_options(stockfish11 PRIVATE -static)
endif()
file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/ship")
message(STATUS "Stockfish 11: ${PCHESS_ENGINE_TARGET}")
