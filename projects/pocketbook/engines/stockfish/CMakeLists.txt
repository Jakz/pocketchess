# SPDX-License-Identifier: GPL-3.0-or-later
# Stockfish engine plug-in (D4, D8). Sources: libs/stockfish/ (local only, fetch-engines.ps1).
# The preprocessor flags replicate the upstream Makefile for ARCH=armv7-neon and x86-64-avx2;
# BUILDING.md records where each flag comes from. Re-check them whenever the pin changes.
cmake_minimum_required(VERSION 3.20)
project(pchess_stockfish CXX)

set(ENGINE_SOURCE_DIR "" CACHE PATH "libs/stockfish")
set(PCHESS_ENGINE_TARGET "arm" CACHE STRING "arm | windows-x64")
option(PCHESS_SF_EMBED_NET "Embed the NNUE network (INCBIN); OFF ships the .nnue beside the binary (R2)" ON)
if(NOT EXISTS "${ENGINE_SOURCE_DIR}/src/uci.cpp")
    message(FATAL_ERROR "Stockfish sources not found in '${ENGINE_SOURCE_DIR}' (run scripts/fetch-engines.ps1)")
endif()
set(SF "${ENGINE_SOURCE_DIR}/src")

# Globbing keeps the list valid across Stockfish versions (files move between releases).
file(GLOB SF_SOURCES "${SF}/*.cpp" "${SF}/nnue/*.cpp" "${SF}/nnue/features/*.cpp" "${SF}/syzygy/*.cpp")
add_executable(stockfish ${SF_SOURCES})
set_target_properties(stockfish PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(stockfish PRIVATE "${SF}")
target_compile_definitions(stockfish PRIVATE NDEBUG USE_PTHREADS)
target_compile_options(stockfish PRIVATE -O3 -fno-exceptions -funroll-loops)

if(PCHESS_ENGINE_TARGET STREQUAL "arm")
    # Makefile ARCH=armv7-neon: arm_version=7, neon=yes, popcnt=yes (32-bit: no IS_64BIT).
    target_compile_definitions(stockfish PRIVATE USE_POPCNT USE_NEON=7)
    target_compile_options(stockfish PRIVATE -mcpu=cortex_a7 -mfpu=neon -mfloat-abi=softfp)
    target_link_options(stockfish PRIVATE -static-libstdc++)
else()
    # Makefile ARCH=x86-64-avx2.
    target_compile_definitions(stockfish PRIVATE IS_64BIT USE_POPCNT USE_AVX2 USE_SSE41 USE_SSSE3 USE_SSE2)
    target_compile_options(stockfish PRIVATE -mavx2 -mbmi -mpopcnt -msse4.1 -mssse3 -msse2)
endif()

# NNUE network(s): the names are read from evaluate.h (EvalFileDefaultNameBig/Small).
file(READ "${SF}/evaluate.h" SF_EVALUATE_H)
string(REGEX MATCHALL "nn-[0-9a-f]+\\.nnue" SF_NETS "${SF_EVALUATE_H}")
list(REMOVE_DUPLICATES SF_NETS)
if(PCHESS_SF_EMBED_NET)
    # INCBIN resolves the file name relative to the assembler's include path.
    target_compile_options(stockfish PRIVATE "SHELL:-Wa,-I${SF}")
else()
    target_compile_definitions(stockfish PRIVATE NNUE_EMBEDDING_OFF)
    foreach(net ${SF_NETS})
        configure_file("${SF}/${net}" "${CMAKE_BINARY_DIR}/${net}" COPYONLY)
    endforeach()
endif()
message(STATUS "Stockfish: ${PCHESS_ENGINE_TARGET}, nets: ${SF_NETS}, embedded: ${PCHESS_SF_EMBED_NET}")
