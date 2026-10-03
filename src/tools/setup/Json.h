// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A small JSON value for editing the reader's launcher file: keeps the order of object members,
// keeps numbers as their original text, writes back with an indentation like the firmware's.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pchess::setup {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string text;                                  // String: the value; Number: as written
    std::vector<Json> items;                           // Array
    std::vector<std::pair<std::string, Json>> members;  // Object, in order

    static Json string(std::string s) {
        Json j;
        j.type = Type::String;
        j.text = std::move(s);
        return j;
    }
    static Json object() {
        Json j;
        j.type = Type::Object;
        return j;
    }

    Json* find(const std::string& key);  // Object member, or nullptr
    void set(const std::string& key, Json value);  // replace in place, or append
    bool erase(const std::string& key);

    static std::optional<Json> parse(const std::string& text);
    std::string dump(int indent = 4) const;
};

} // namespace pchess::setup
