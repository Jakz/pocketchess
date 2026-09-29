// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/EngineTypes.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace pchess::engine {

namespace {
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
} // namespace

const char* toString(Role r) {
    switch (r) {
        case Role::Play: return "play";
        case Role::Analysis: return "analysis";
        case Role::Hint: return "hint";
        case Role::Validate: return "validate";
    }
    return "?";
}

std::optional<Role> roleFromString(std::string_view s) {
    for (Role r : {Role::Play, Role::Analysis, Role::Hint, Role::Validate})
        if (iequals(s, toString(r))) return r;
    return std::nullopt;
}

const char* toString(EnergyClass e) {
    switch (e) {
        case EnergyClass::Low: return "low";
        case EnergyClass::Medium: return "medium";
        case EnergyClass::High: return "high";
    }
    return "?";
}

std::optional<EnergyClass> energyClassFromString(std::string_view s) {
    for (EnergyClass e : {EnergyClass::Low, EnergyClass::Medium, EnergyClass::High})
        if (iequals(s, toString(e))) return e;
    return std::nullopt;
}

void setOption(OptionSet& set, const std::string& name, const std::string& value) {
    for (auto& [k, v] : set)
        if (iequals(k, name)) {
            v = value;
            return;
        }
    set.emplace_back(name, value);
}

const std::string* findOption(const OptionSet& set, std::string_view name) {
    for (const auto& [k, v] : set)
        if (iequals(k, name)) return &v;
    return nullptr;
}

std::string SearchLimits::goCommand() const {
    std::string s = "go";
    if (depth) s += " depth " + std::to_string(*depth);
    if (nodes) s += " nodes " + std::to_string(*nodes);
    if (movetimeMs) s += " movetime " + std::to_string(*movetimeMs);
    if (infinite || !bounded()) s += " infinite";
    return s;
}

SearchLimits SearchLimits::overriddenBy(const SearchLimits& o) const {
    SearchLimits r = *this;
    if (o.movetimeMs) r.movetimeMs = o.movetimeMs;
    if (o.depth) r.depth = o.depth;
    if (o.nodes) r.nodes = o.nodes;
    if (o.infinite) r.infinite = true;
    return r;
}

const UciOption* Capabilities::find(std::string_view optionName) const {
    for (const auto& o : options)
        if (iequals(o.name, optionName)) return &o;
    return nullptr;
}

OptionSet validateOptions(const OptionSet& wanted, const Capabilities& caps, std::vector<std::string>* warnings) {
    OptionSet out;
    auto warn = [&](const std::string& w) {
        if (warnings) warnings->push_back(w);
    };
    for (const auto& [name, value] : wanted) {
        const UciOption* o = caps.find(name);
        if (!o) {
            warn("option '" + name + "' not offered by " + (caps.name.empty() ? "the engine" : caps.name) +
                 ", dropped");
            continue;
        }
        std::string v = value;
        if (o->type == "spin") {
            long long n = 0;
            const auto r = std::from_chars(v.data(), v.data() + v.size(), n);
            if (r.ec != std::errc() || r.ptr != v.data() + v.size()) {
                warn("option '" + o->name + "': '" + v + "' is not a number, dropped");
                continue;
            }
            long long c = n;
            if (o->min) c = std::max<long long>(c, *o->min);
            if (o->max) c = std::min<long long>(c, *o->max);
            if (c != n) warn("option '" + o->name + "': " + v + " clamped to " + std::to_string(c));
            v = std::to_string(c);
        } else if (o->type == "check") {
            if (iequals(v, "true") || v == "1" || iequals(v, "yes") || iequals(v, "on"))
                v = "true";
            else if (iequals(v, "false") || v == "0" || iequals(v, "no") || iequals(v, "off"))
                v = "false";
            else {
                warn("option '" + o->name + "': '" + v + "' is not true/false, dropped");
                continue;
            }
        } else if (o->type == "combo") {
            auto it = std::find_if(o->vars.begin(), o->vars.end(), [&](const std::string& s) { return iequals(s, v); });
            if (it == o->vars.end()) {
                warn("option '" + o->name + "': '" + v + "' is not one of its values, dropped");
                continue;
            }
            v = *it;
        } else if (o->type == "button") {
            v.clear();
        }
        out.emplace_back(o->name, v);  // the engine's spelling of the name
    }
    return out;
}

} // namespace pchess::engine
