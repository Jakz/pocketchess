# SPDX-License-Identifier: GPL-3.0-or-later
# PocketBook build (build doc §4.1). Configure through build-zig.ps1, which passes the shared
# Zig toolchain file (D3). Targets: pchess-core, pchess-ui (static), pchess-pocketbook
# (pocketchess.app) and pchess-selftest (pocketchess-selftest.app).
cmake_minimum_required(VERSION 3.20)
project(PocketChessPocketBook LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "This project only cross-compiles for PocketBook. Use projects/pocketbook/build-zig.ps1 "
                        "(the desktop build is projects/msvc/PocketChess.sln).")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(POCKETBOOK_TOOLCHAIN_ROOT "" CACHE PATH "Shared Zig/InkView toolchain (C:/Users/jack/Documents/Dev/libs/pocketbook)")
if(NOT POCKETBOOK_SDK_ROOT)
    set(POCKETBOOK_SDK_ROOT "${POCKETBOOK_TOOLCHAIN_ROOT}/inkview/6.10" CACHE PATH "InkView SDK" FORCE)
endif()

find_path(INKVIEW_INCLUDE_DIR inkview.h
    PATHS "${POCKETBOOK_SDK_ROOT}" PATH_SUFFIXES include usr/include
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_library(INKVIEW_LIBRARY NAMES inkview libinkview.so
    PATHS "${POCKETBOOK_SDK_ROOT}" PATH_SUFFIXES lib usr/lib
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT INKVIEW_INCLUDE_DIR OR NOT INKVIEW_LIBRARY)
    message(FATAL_ERROR "InkView not found under ${POCKETBOOK_SDK_ROOT} (inkview.h / libinkview.so)")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# -ffp-contract=off: no FMA contraction, so the canvas stays bit-identical to the desktop
# build (golden checksums in the self-test). No -fsigned-char: the code must be correct with
# ARM's unsigned char, which the self-test checks.
add_compile_options(-Wall -Wextra -ffp-contract=off)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
else()
    add_compile_options(-O0 -g)
endif()

# InkView headers pull in zlib/freetype headers the SDK may not ship (Enigmistica's compat
# handling): prefer the toolchain's compat/ folder, then the SDK's sibling include dirs.
set(PCHESS_INKVIEW_INCLUDES "${INKVIEW_INCLUDE_DIR}")
set(PCHESS_INKVIEW_OPTIONS "")
if(NOT EXISTS "${INKVIEW_INCLUDE_DIR}/zlib.h" AND EXISTS "${POCKETBOOK_TOOLCHAIN_ROOT}/compat/zlib.h")
    list(APPEND PCHESS_INKVIEW_INCLUDES "${POCKETBOOK_TOOLCHAIN_ROOT}/compat")
endif()
foreach(dir "${POCKETBOOK_SDK_ROOT}/../include" "${POCKETBOOK_SDK_ROOT}/../include/freetype2")
    if(EXISTS "${dir}")
        list(APPEND PCHESS_INKVIEW_OPTIONS "-idirafter" "${dir}")
    endif()
endforeach()

add_library(pchess-core STATIC ${PCHESS_CORE_SOURCES})
target_include_directories(pchess-core PUBLIC "${REPO_ROOT}/src")
target_link_libraries(pchess-core PUBLIC Threads::Threads)

add_library(pchess-ui STATIC ${PCHESS_UI_SOURCES})
target_link_libraries(pchess-ui PUBLIC pchess-core)

# OBJECT library: core's engine code calls Process::spawn (PosixProcess.cpp), so these objects
# must be linked directly rather than from an archive scanned before pchess-core.
add_library(pchess-platform-pb OBJECT ${PCHESS_PLATFORM_PB_SOURCES})
target_include_directories(pchess-platform-pb PRIVATE ${PCHESS_INKVIEW_INCLUDES})
target_compile_options(pchess-platform-pb PRIVATE ${PCHESS_INKVIEW_OPTIONS})
target_link_libraries(pchess-platform-pb PUBLIC pchess-ui)

add_executable(pchess-pocketbook "${REPO_ROOT}/src/platform/pocketbook/Main.cpp")
set_target_properties(pchess-pocketbook PROPERTIES OUTPUT_NAME pocketchess SUFFIX ".app")
target_include_directories(pchess-pocketbook PRIVATE ${PCHESS_INKVIEW_INCLUDES})
target_compile_options(pchess-pocketbook PRIVATE ${PCHESS_INKVIEW_OPTIONS})
target_link_libraries(pchess-pocketbook PRIVATE pchess-platform-pb "${INKVIEW_LIBRARY}")

add_executable(pchess-selftest "${REPO_ROOT}/src/platform/pocketbook/SelfTestMain.cpp" ${PCHESS_TEST_SOURCES})
set_target_properties(pchess-selftest PROPERTIES OUTPUT_NAME pocketchess-selftest SUFFIX ".app")
target_include_directories(pchess-selftest PRIVATE "${REPO_ROOT}/tests" ${PCHESS_INKVIEW_INCLUDES})
target_compile_options(pchess-selftest PRIVATE ${PCHESS_INKVIEW_OPTIONS})
target_link_libraries(pchess-selftest PRIVATE pchess-platform-pb "${INKVIEW_LIBRARY}")
