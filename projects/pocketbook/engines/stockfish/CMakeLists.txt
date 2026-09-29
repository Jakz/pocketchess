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
target_compile_definitions(stockfish PRIVATE NDEBUG)
target_compile_options(stockfish PRIVATE -O3 -fno-exceptions -funroll-loops)

if(PCHESS_ENGINE_TARGET STREQUAL "arm")
    # Makefile ARCH=armv7-neon: arm_version=7, neon=yes, popcnt=yes (32-bit: no IS_64BIT).
    target_compile_definitions(stockfish PRIVATE USE_POPCNT USE_NEON=7)
    # Zig links its libc++ statically, like the app (no -static-libstdc++ needed).
    target_compile_options(stockfish PRIVATE -mcpu=cortex_a7 -mfpu=neon -mfloat-abi=softfp)
else()
    # Makefile ARCH=x86-64-avx2.
    target_compile_definitions(stockfish PRIVATE IS_64BIT USE_POPCNT USE_AVX2 USE_SSE41 USE_SSSE3 USE_SSE2)
    target_compile_options(stockfish PRIVATE -mavx2 -mbmi -mpopcnt -msse4.1 -mssse3 -msse2)
endif()

# Threads: thread_native.h uses pthreads with every compiler except MSVC (Makefile: -lpthread;
# on Windows/mingw that is winpthreads).
target_link_libraries(stockfish PRIVATE pthread)
if(WIN32)
    # misc.cpp uses the large-page privilege APIs; one self-contained .exe (Makefile: -static).
    target_link_libraries(stockfish PRIVATE advapi32)
    target_link_options(stockfish PRIVATE -static)
endif()

# NNUE network(s): the names are read from evaluate.h (EvalFileDefaultNameBig/Small).
file(READ "${SF}/evaluate.h" SF_EVALUATE_H)
string(REGEX MATCHALL "nn-[0-9a-f]+\\.nnue" SF_NETS "${SF_EVALUATE_H}")
list(REMOVE_DUPLICATES SF_NETS)
# The nets go into the build folder either way: INCBIN's .incbin "nn-....nnue" is resolved
# relative to the compiler's working directory, which is the build folder under Ninja (the
# upstream Makefile compiles inside src/). Without embedding they ship beside the binary.
foreach(net ${SF_NETS})
    if(NOT EXISTS "${SF}/${net}")
        message(FATAL_ERROR "${SF}/${net} missing (run scripts/fetch-engines.ps1)")
    endif()
    configure_file("${SF}/${net}" "${CMAKE_BINARY_DIR}/${net}" COPYONLY)
endforeach()
# Files to ship beside the binary go to ship/ (build-engine.ps1 copies that folder).
file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/ship")
if(NOT PCHESS_SF_EMBED_NET)
    target_compile_definitions(stockfish PRIVATE NNUE_EMBEDDING_OFF)
    foreach(net ${SF_NETS})
        configure_file("${SF}/${net}" "${CMAKE_BINARY_DIR}/ship/${net}" COPYONLY)
    endforeach()
endif()
message(STATUS "Stockfish: ${PCHESS_ENGINE_TARGET}, nets: ${SF_NETS}, embedded: ${PCHESS_SF_EMBED_NET}")
