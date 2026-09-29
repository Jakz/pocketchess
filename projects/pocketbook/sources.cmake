# SPDX-License-Identifier: GPL-3.0-or-later
# Source lists shared with the MSVC projects (parity rule, build doc §6).
# scripts/check-sources.ps1 compares these lists with the ClCompile items of
# PchessCore.vcxproj, PchessUi.vcxproj and PocketChessTests.vcxproj and fails on any difference.

set(PCHESS_CORE_SOURCES
    ${REPO_ROOT}/src/core/chess/Bitboard.cpp
    ${REPO_ROOT}/src/core/chess/Fen.cpp
    ${REPO_ROOT}/src/core/chess/GameTree.cpp
    ${REPO_ROOT}/src/core/chess/MoveCodec.cpp
    ${REPO_ROOT}/src/core/chess/MoveGen.cpp
    ${REPO_ROOT}/src/core/chess/Pgn.cpp
    ${REPO_ROOT}/src/core/chess/Position.cpp
    ${REPO_ROOT}/src/core/chess/Rules.cpp
    ${REPO_ROOT}/src/core/chess/San.cpp
    ${REPO_ROOT}/src/core/chess/Types.cpp
    ${REPO_ROOT}/src/core/chess/Zobrist.cpp
    ${REPO_ROOT}/src/core/engine/EngineDescriptor.cpp
    ${REPO_ROOT}/src/core/engine/EngineManager.cpp
    ${REPO_ROOT}/src/core/engine/EngineRegistry.cpp
    ${REPO_ROOT}/src/core/engine/EngineTypes.cpp
    ${REPO_ROOT}/src/core/engine/PowerProfile.cpp
    ${REPO_ROOT}/src/core/engine/UciEngine.cpp
    ${REPO_ROOT}/src/core/engine/UciParser.cpp
    ${REPO_ROOT}/src/core/engine/UciProbe.cpp
    ${REPO_ROOT}/src/core/util/Clock.cpp
    ${REPO_ROOT}/src/core/util/Error.cpp
    ${REPO_ROOT}/src/core/util/Log.cpp
    ${REPO_ROOT}/src/core/util/Paths.cpp
    ${REPO_ROOT}/src/core/util/Utf8.cpp
    ${REPO_ROOT}/src/core/util/Yaml.cpp
)

set(PCHESS_UI_SOURCES
    ${REPO_ROOT}/src/app/AnalyzeScreen.cpp
    ${REPO_ROOT}/src/app/Application.cpp
    ${REPO_ROOT}/src/app/Chrome.cpp
    ${REPO_ROOT}/src/app/CommandLine.cpp
    ${REPO_ROOT}/src/app/DiagnosticsScreen.cpp
    ${REPO_ROOT}/src/app/Dialogs.cpp
    ${REPO_ROOT}/src/app/HeadlessHost.cpp
    ${REPO_ROOT}/src/app/HomeScreen.cpp
    ${REPO_ROOT}/src/app/Screenshot.cpp
    ${REPO_ROOT}/src/app/Script.cpp
    ${REPO_ROOT}/src/ui/board/BoardView.cpp
    ${REPO_ROOT}/src/ui/canvas/Canvas.cpp
    ${REPO_ROOT}/src/ui/canvas/Color.cpp
    ${REPO_ROOT}/src/ui/canvas/Font.cpp
    ${REPO_ROOT}/src/ui/canvas/PngWriter.cpp
    ${REPO_ROOT}/src/ui/canvas/Rasterizer.cpp
    ${REPO_ROOT}/src/ui/canvas/TrueTypeFont.cpp
    ${REPO_ROOT}/src/ui/input/Input.cpp
    ${REPO_ROOT}/src/ui/render/Refresh.cpp
    ${REPO_ROOT}/src/ui/theme/BoardTheme.cpp
    ${REPO_ROOT}/src/ui/theme/PieceSet.cpp
    ${REPO_ROOT}/src/ui/widgets/Widgets.cpp
)

set(PCHESS_PLATFORM_PB_SOURCES
    ${REPO_ROOT}/src/platform/pocketbook/InkViewPlatform.cpp
    ${REPO_ROOT}/src/platform/posix/PosixProcess.cpp
)

set(PCHESS_TEST_SOURCES
    ${REPO_ROOT}/tests/app/AnalysisPanelTests.cpp
    ${REPO_ROOT}/tests/app/AppTests.cpp
    ${REPO_ROOT}/tests/core/ChessTests.cpp
    ${REPO_ROOT}/tests/core/EngineDescriptorTests.cpp
    ${REPO_ROOT}/tests/core/EngineIntegrationTests.cpp
    ${REPO_ROOT}/tests/core/EngineManagerTests.cpp
    ${REPO_ROOT}/tests/core/EngineTests.cpp
    ${REPO_ROOT}/tests/core/PerftTests.cpp
    ${REPO_ROOT}/tests/core/UciEngineTests.cpp
    ${REPO_ROOT}/tests/core/YamlTests.cpp
    ${REPO_ROOT}/tests/support/FakeEngine.cpp
    ${REPO_ROOT}/tests/support/Golden.cpp
    ${REPO_ROOT}/tests/support/Test.cpp
    ${REPO_ROOT}/tests/support/TestTools.cpp
    ${REPO_ROOT}/tests/ui/UiTests.cpp
)

