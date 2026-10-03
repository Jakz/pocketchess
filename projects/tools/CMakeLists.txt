# SPDX-License-Identifier: GPL-3.0-or-later
# Desktop tools for every installer platform (ADR 0019): pocketchess-setup, PgnImporter and
# PuzzleImporter, cross-compiled with Zig (zig-tool.cmake, target in ZIG_TARGET) by build-tools.ps1.
# Windows developers also get them from the MSVC solution.
cmake_minimum_required(VERSION 3.20)
project(PocketChessTools LANGUAGES C CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${REPO_ROOT}/projects/pocketbook/sources.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-O2 -ffunction-sections -fdata-sections -Wno-nullability-completeness)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_link_options(-Wl,--gc-sections -s)
endif()

add_library(tools-sqlite STATIC "${REPO_ROOT}/libs/sqlite/sqlite3.c")
target_include_directories(tools-sqlite PUBLIC "${REPO_ROOT}/libs/sqlite")
target_compile_definitions(tools-sqlite PRIVATE SQLITE_CUSTOM_INCLUDE=pchess_sqlite_config.h)
target_compile_options(tools-sqlite PRIVATE -w)

add_library(tools-zstd STATIC "${REPO_ROOT}/libs/zstd/zstddeclib.c")
target_include_directories(tools-zstd PUBLIC "${REPO_ROOT}/libs/zstd")
target_compile_options(tools-zstd PRIVATE -w)

# The importers only use the content code of core (games, puzzles, storage, chess): the engine
# objects in the archive are never linked.
add_library(tools-core STATIC ${PCHESS_CORE_SOURCES})
target_include_directories(tools-core PUBLIC "${REPO_ROOT}/src")
target_link_libraries(tools-core PUBLIC tools-sqlite)

add_library(tools-common STATIC "${REPO_ROOT}/src/tools/common/LineReader.cpp")
target_link_libraries(tools-common PUBLIC tools-core tools-zstd)

add_executable(PgnImporter "${REPO_ROOT}/src/tools/pgn_importer/Main.cpp")
target_link_libraries(PgnImporter PRIVATE tools-common)
add_executable(PuzzleImporter "${REPO_ROOT}/src/tools/puzzle_importer/Main.cpp")
target_link_libraries(PuzzleImporter PRIVATE tools-common)

add_executable(pocketchess-setup
    "${REPO_ROOT}/src/tools/setup/Fetch.cpp"
    "${REPO_ROOT}/src/tools/setup/Json.cpp"
    "${REPO_ROOT}/src/tools/setup/Launcher.cpp"
    "${REPO_ROOT}/src/tools/setup/Main.cpp"
    "${REPO_ROOT}/src/tools/setup/Reader.cpp")
target_include_directories(pocketchess-setup PRIVATE "${REPO_ROOT}/src")
