// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Script.h"

#include "app/Application.h"
#include "core/chess/Types.h"
#include "core/util/Utf8.h"

#include <charconv>

namespace pchess::app {

using namespace ui;

namespace {

std::optional<Point> parsePoint(const Application& app, std::string_view s) {
    const chess::Square sq = chess::parseSquare(s);
    if (sq != chess::NoSquare) {
        const Layout& l = app.layout();
        return Point{l.board.x + chess::fileOf(sq) * l.square + l.square / 2,
                     l.board.y + (7 - chess::rankOf(sq)) * l.square + l.square / 2};
    }
    const size_t comma = s.find(',');
    if (comma == std::string_view::npos) return std::nullopt;
    int x = 0, y = 0;
    auto a = std::from_chars(s.data(), s.data() + comma, x);
    auto b = std::from_chars(s.data() + comma + 1, s.data() + s.size(), y);
    if (a.ec != std::errc() || b.ec != std::errc()) return std::nullopt;
    return Point{x, y};
}

std::optional<KeyCode> parseKey(std::string_view s) {
    static const std::pair<const char*, KeyCode> keys[] = {
        {"prev", KeyCode::Prev}, {"next", KeyCode::Next}, {"menu", KeyCode::Menu}, {"home", KeyCode::Home},
        {"back", KeyCode::Back}, {"up", KeyCode::Up},     {"down", KeyCode::Down}, {"left", KeyCode::Left},
        {"right", KeyCode::Right}, {"ok", KeyCode::Ok},
    };
    for (const auto& [name, k] : keys)
        if (s == name) return k;
    return std::nullopt;
}

} // namespace

Result<void> runScript(Application& app, const std::string& script, const std::function<void(int ms)>& wait) {
    size_t i = 0;
    while (i <= script.size()) {
        size_t j = script.find(';', i);
        if (j == std::string::npos) j = script.size();
        const std::string cmd = util::trim(std::string_view(script).substr(i, j - i));
        i = j + 1;
        if (cmd.empty()) continue;
        const size_t colon = cmd.find(':');
        if (colon == std::string::npos) return makeError(ErrorCode::InvalidArgument, "script: missing ':' in " + cmd);
        const std::string op = cmd.substr(0, colon);
        const std::string arg = cmd.substr(colon + 1);
        using T = InputEvent::Type;
        if (op == "tap" || op == "long") {
            auto p = parsePoint(app, arg);
            if (!p) return makeError(ErrorCode::InvalidArgument, "script: bad point " + arg);
            app.handleInput(InputEvent::pointer(T::PointerDown, *p));
            if (op == "long") app.handleInput(InputEvent::pointer(T::LongPress, *p));
            app.handleInput(InputEvent::pointer(T::PointerUp, *p));
        } else if (op == "drag") {
            const size_t dash = arg.find('-');
            auto a = dash == std::string::npos ? std::nullopt : parsePoint(app, arg.substr(0, dash));
            auto b = dash == std::string::npos ? std::nullopt : parsePoint(app, arg.substr(dash + 1));
            if (!a || !b) return makeError(ErrorCode::InvalidArgument, "script: bad drag " + arg);
            app.handleInput(InputEvent::pointer(T::PointerDown, *a));
            app.handleInput(InputEvent::pointer(T::PointerMove, *b));
            app.handleInput(InputEvent::pointer(T::PointerUp, *b));
        } else if (op == "key") {
            auto k = parseKey(arg);
            if (!k) return makeError(ErrorCode::InvalidArgument, "script: bad key " + arg);
            app.handleInput(InputEvent::keyPress(*k));
        } else if (op == "text") {
            app.handleInput(InputEvent::typed(arg));
        } else if (op == "wait") {
            int ms = 0;
            std::from_chars(arg.data(), arg.data() + arg.size(), ms);
            if (wait) wait(ms);
        } else {
            return makeError(ErrorCode::InvalidArgument, "script: unknown command " + op);
        }
    }
    app.flush();
    return {};
}

} // namespace pchess::app
