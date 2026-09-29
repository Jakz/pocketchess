// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pchess::util {

// A small YAML subset reader for descriptors (engines, themes, content packs; ADR 0007).
//
// Supported: block mappings and sequences (by indentation, spaces only), "- key: value" items,
// flow collections ({a: 1, b: [x, y]}, may span lines), plain, 'single' and "double" quoted
// scalars, comments, `~`/`null`/empty as null, a leading `---`.
// Rejected with an error (never misread): tabs in indentation, anchors/aliases (& *), tags (!),
// block scalars (| >), duplicate keys, bad indentation, unbalanced flow collections.
//
// Mappings keep their order. Lookups of missing keys return a shared Null node, so
// `doc["a"]["b"].asInt()` never throws or crashes.
class YamlNode {
public:
    enum class Kind : uint8_t { Null, Scalar, Sequence, Map };
    using Entry = std::pair<std::string, YamlNode>;

    YamlNode() = default;
    static YamlNode scalar(std::string text, bool quoted, int line);
    static YamlNode sequence(int line);
    static YamlNode map(int line);

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool isScalar() const { return kind_ == Kind::Scalar; }
    bool isSequence() const { return kind_ == Kind::Sequence; }
    bool isMap() const { return kind_ == Kind::Map; }
    int line() const { return line_; }  // 1-based source line, 0 = synthetic

    // Scalar text (unquoted, escapes resolved); empty for other kinds.
    const std::string& text() const { return text_; }
    bool quoted() const { return quoted_; }

    // Sequence items / map entries (empty for other kinds).
    size_t size() const;
    const std::vector<YamlNode>& items() const { return items_; }
    const std::vector<Entry>& entries() const { return entries_; }
    const YamlNode& operator[](size_t index) const;
    const YamlNode& operator[](std::string_view key) const;
    const YamlNode* find(std::string_view key) const;
    bool has(std::string_view key) const { return find(key) != nullptr; }

    // Typed scalar access; nullopt when the node is not a scalar of that type.
    std::optional<std::string> asString() const;  // scalars only
    std::optional<int64_t> asInt() const;
    std::optional<double> asDouble() const;
    std::optional<bool> asBool() const;  // true/false/yes/no/on/off, any case
    std::string stringOr(std::string fallback) const { return asString().value_or(std::move(fallback)); }
    int64_t intOr(int64_t fallback) const { return asInt().value_or(fallback); }
    bool boolOr(bool fallback) const { return asBool().value_or(fallback); }
    // Scalars and sequences of scalars as a list of strings (a single scalar gives one item).
    std::vector<std::string> stringList() const;

    // Building (parser, tests).
    void push(YamlNode item) { items_.push_back(std::move(item)); }
    bool insert(std::string key, YamlNode value);  // false on a duplicate key

private:
    Kind kind_ = Kind::Null;
    bool quoted_ = false;
    int line_ = 0;
    std::string text_;
    std::vector<YamlNode> items_;
    std::vector<Entry> entries_;
};

// Errors carry "line N: ..." in their context.
Result<YamlNode> parseYaml(std::string_view text);
Result<YamlNode> loadYamlFile(const std::filesystem::path& file);

} // namespace pchess::util
