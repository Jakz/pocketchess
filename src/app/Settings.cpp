// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Settings.h"

#include <charconv>

namespace pchess::app {

namespace {

const char* boolText(bool b) { return b ? "true" : "false"; }

void readBool(const std::map<std::string, std::string>& m, const char* key, bool& out) {
    auto it = m.find(key);
    if (it == m.end()) return;
    if (it->second == "true") out = true;
    else if (it->second == "false") out = false;
}

void readInt(const std::map<std::string, std::string>& m, const char* key, int& out, int lo, int hi) {
    auto it = m.find(key);
    if (it == m.end()) return;
    int v = 0;
    const std::string& s = it->second;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec == std::errc() && p == s.data() + s.size() && v >= lo && v <= hi) out = v;
}

void readString(const std::map<std::string, std::string>& m, const char* key, std::string& out) {
    if (auto it = m.find(key); it != m.end()) out = it->second;
}

} // namespace

std::map<std::string, std::string> Settings::toMap() const {
    return {
        {"display.board_theme", boardTheme},
        {"display.mono_theme", monoTheme},
        {"display.piece_set", pieceSet},
        {"display.mono", boolText(mono)},
        {"display.coordinates", boolText(coordinates)},
        {"display.full_refresh_every", std::to_string(fullRefreshEvery)},
        {"input.tap_to_move", boolText(tapToMove)},
        {"input.drag_to_move", boolText(dragToMove)},
        {"input.show_legal_moves", boolText(showLegalMoves)},
        {"input.swap_page_keys", boolText(swapPageKeys)},
        {"engine.power_profile", engine::toString(powerProfile)},
        {"engine.analysis_engine", analysisEngine},
        {"engine.play_engine", playEngine},
        {"engine.play_strength", playStrength},
        {"engine.threads", std::to_string(threads)},
        {"engine.hash_mb", std::to_string(hashMb)},
        {"engine.deep_limit_s", std::to_string(deepLimitSeconds)},
        {"engine.multipv", std::to_string(analysisMultiPv)},
        {"engine.eval_from_white", boolText(evalFromWhite)},
        {"tactics.pack", tacticsPack},
        {"games.pack", gamesPack},
        {"tactics.theme", tacticsTheme},
        {"tactics.skip_theme", tacticsSkipTheme},
        {"tactics.difficulty", std::to_string(tacticsDifficulty)},
        {"tactics.auto_next", boolText(tacticsAutoNext)},
        {"tactics.auto_opponent", boolText(tacticsAutoOpponent)},
        {"advanced.clock_offset_days", std::to_string(clockOffsetDays)},
        {"advanced.debug_logging", boolText(debugLogging)},
    };
}

void Settings::fromMap(const std::map<std::string, std::string>& m) {
    readString(m, "display.board_theme", boardTheme);
    readString(m, "display.mono_theme", monoTheme);
    readString(m, "display.piece_set", pieceSet);
    readBool(m, "display.mono", mono);
    readBool(m, "display.coordinates", coordinates);
    readInt(m, "display.full_refresh_every", fullRefreshEvery, 1, 1000);
    readBool(m, "input.tap_to_move", tapToMove);
    readBool(m, "input.drag_to_move", dragToMove);
    readBool(m, "input.show_legal_moves", showLegalMoves);
    readBool(m, "input.swap_page_keys", swapPageKeys);
    if (auto it = m.find("engine.power_profile"); it != m.end())
        if (auto p = engine::powerProfileFromString(it->second)) powerProfile = *p;
    readString(m, "engine.analysis_engine", analysisEngine);
    readString(m, "engine.play_engine", playEngine);
    readString(m, "engine.play_strength", playStrength);
    readInt(m, "engine.threads", threads, 0, 64);
    readInt(m, "engine.hash_mb", hashMb, 0, 4096);
    readInt(m, "engine.deep_limit_s", deepLimitSeconds, -1, 3600);
    readInt(m, "engine.multipv", analysisMultiPv, 1, 3);
    readBool(m, "engine.eval_from_white", evalFromWhite);
    readString(m, "tactics.pack", tacticsPack);
    readString(m, "games.pack", gamesPack);
    readString(m, "tactics.theme", tacticsTheme);
    readString(m, "tactics.skip_theme", tacticsSkipTheme);
    readInt(m, "tactics.difficulty", tacticsDifficulty, -400, 400);
    readBool(m, "tactics.auto_next", tacticsAutoNext);
    readBool(m, "tactics.auto_opponent", tacticsAutoOpponent);
    readInt(m, "advanced.clock_offset_days", clockOffsetDays, -3650, 3650);
    readBool(m, "advanced.debug_logging", debugLogging);
}

} // namespace pchess::app
