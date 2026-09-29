# SPDX-License-Identifier: GPL-3.0-or-later
# Stockfish plug-in "stockfish17": the shared NNUE Stockfish build (../StockfishNnue.cmake).
cmake_minimum_required(VERSION 3.20)
project(pchess_stockfish17 CXX)
set(SF_ID stockfish17)
include("${CMAKE_CURRENT_LIST_DIR}/../StockfishNnue.cmake")
