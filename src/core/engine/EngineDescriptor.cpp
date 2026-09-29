// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/EngineDescriptor.h"

#include <algorithm>
#include <cctype>

namespace pchess::engine {

namespace {

Error bad(const std::filesystem::path& dir, int line, const std::string& msg) {
    std::string ctx = (dir / "engine.yaml").string();
    if (line > 0) ctx += ": line " + std::to_string(line);
    return makeError(ErrorCode::CorruptData, ctx + ": " + msg);
}

// Scalar option values as UCI text ("true"/"false" for booleans).
std::optional<std::string> optionText(const util::YamlNode& v) {
    if (!v.isScalar()) return std::nullopt;
    if (!v.quoted()) {
        if (auto b = v.asBool()) return std::string(*b ? "true" : "false");
    }
    return v.text();
}

Result<OptionSet> parseOptions(const util::YamlNode& n, const std::filesystem::path& dir, const char* what) {
    OptionSet out;
    if (n.isNull()) return out;
    if (!n.isMap()) return bad(dir, n.line(), std::string(what) + " must be a mapping");
    for (const auto& [k, v] : n.entries()) {
        auto t = optionText(v);
        if (!t) return bad(dir, v.line() ? v.line() : n.line(), std::string(what) + ": '" + k + "' needs a value");
        out.emplace_back(k, *t);
    }
    return out;
}

Result<SearchLimits> parseLimits(const util::YamlNode& n, const std::filesystem::path& dir) {
    SearchLimits l;
    if (n.isNull()) return l;
    if (!n.isMap()) return bad(dir, n.line(), "limits must be a mapping");
    for (const auto& [k, v] : n.entries()) {
        const auto i = v.asInt();
        if (!i || *i <= 0) return bad(dir, v.line(), "limits: '" + k + "' must be a positive number");
        if (k == "movetime")
            l.movetimeMs = static_cast<int>(*i);
        else if (k == "depth")
            l.depth = static_cast<int>(*i);
        else if (k == "nodes")
            l.nodes = static_cast<uint64_t>(*i);
        else
            return bad(dir, v.line(), "limits: unknown key '" + k + "' (movetime, depth, nodes)");
    }
    return l;
}

std::optional<int> optInt(const util::YamlNode& n) {
    if (auto i = n.asInt()) return static_cast<int>(*i);
    return std::nullopt;
}

} // namespace

bool EngineDescriptor::supports(Role r) const {
    return std::find(roles.begin(), roles.end(), r) != roles.end();
}

const StrengthLevel* EngineDescriptor::strengthFor(std::string_view label) const {
    for (const auto& s : strength)
        if (s.label == label) return &s;
    return nullptr;
}

std::filesystem::path EngineDescriptor::binaryPath(const std::string& platform) const {
    const auto it = binaries.find(platform);
    if (it == binaries.end() || it->second.empty()) return {};
    const std::filesystem::path p(it->second);
    return p.is_absolute() ? p : dir / p;
}

const char* currentPlatformKey() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "windows-x64";
#elif defined(_WIN32)
    return "windows-x86";
#elif defined(__arm__)
    return "arm";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__x86_64__)
    return "linux-x64";
#else
    return "unknown";
#endif
}

const std::vector<std::string>& strengthLabels() {
    static const std::vector<std::string> labels{"beginner", "casual", "club", "strong", "maximum"};
    return labels;
}

Result<EngineDescriptor> parseDescriptor(const util::YamlNode& doc, const std::filesystem::path& dir) {
    if (!doc.isMap()) return bad(dir, doc.line(), "expected a mapping at the top level");
    EngineDescriptor d;
    d.dir = dir;
    d.id = doc["id"].stringOr("");
    if (d.id.empty()) return bad(dir, 0, "missing 'id'");
    for (char c : d.id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
            return bad(dir, doc["id"].line(), "id may only contain letters, digits, '-' and '_'");
    d.name = doc["name"].stringOr(d.id);
    d.protocol = doc["protocol"].stringOr("uci");
    if (d.protocol != "uci") return bad(dir, doc["protocol"].line(), "unsupported protocol '" + d.protocol + "'");
    d.license = doc["license"].stringOr("");

    const util::YamlNode& bin = doc["binary"];
    if (bin.isScalar()) {
        d.binaries[currentPlatformKey()] = bin.text();
    } else if (bin.isMap()) {
        for (const auto& [k, v] : bin.entries())
            if (v.isScalar()) d.binaries[k] = v.text();
    } else {
        return bad(dir, bin.line(), "missing 'binary' (a file name, or a map of platform -> file name)");
    }
    d.args = doc["args"].stringList();
    d.files = doc["files"].stringList();

    const util::YamlNode& roles = doc["roles"];
    if (roles.isNull()) {
        d.roles = {Role::Play, Role::Analysis, Role::Hint, Role::Validate};
    } else {
        for (const auto& r : roles.stringList()) {
            auto role = roleFromString(r);
            if (!role) return bad(dir, roles.line(), "unknown role '" + r + "' (play, analysis, hint, validate)");
            if (!d.supports(*role)) d.roles.push_back(*role);
        }
    }

    const util::YamlNode& res = doc["resources"];
    d.resources.diskMb = optInt(res["disk_mb"]);
    d.resources.rssMb = optInt(res["rss_mb"]);
    d.resources.startupMs = optInt(res["startup_ms"]);
    if (const auto& ec = res["energy_class"]; ec.isScalar()) {
        auto e = energyClassFromString(ec.text());
        if (!e) return bad(dir, ec.line(), "energy_class must be low, medium or high");
        d.resources.energy = *e;
    }

    auto defaults = parseOptions(doc["defaults"], dir, "defaults");
    if (!defaults) return defaults.error();
    d.defaults = std::move(*defaults);

    const util::YamlNode& strength = doc["strength"];
    if (!strength.isNull() && !strength.isMap()) return bad(dir, strength.line(), "strength must be a mapping");
    for (const auto& [label, level] : strength.entries()) {
        StrengthLevel s;
        s.label = label;
        if (!level.isNull() && !level.isMap())
            return bad(dir, level.line(), "strength." + label + " must be a mapping");
        auto opts = parseOptions(level["options"], dir, "strength options");
        if (!opts) return opts.error();
        s.options = std::move(*opts);
        auto limits = parseLimits(level["limits"], dir);
        if (!limits) return limits.error();
        s.limits = *limits;
        d.strength.push_back(std::move(s));
    }
    return d;
}

Result<EngineDescriptor> loadDescriptor(const std::filesystem::path& yamlFile) {
    auto doc = util::loadYamlFile(yamlFile);
    if (!doc)
        return makeError(doc.error().code, yamlFile.parent_path().filename().string() + "/" + doc.error().context);
    return parseDescriptor(*doc, yamlFile.parent_path());
}

} // namespace pchess::engine
