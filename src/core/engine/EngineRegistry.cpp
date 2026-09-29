// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine/EngineRegistry.h"

#include "core/util/Log.h"

#include <algorithm>
#include <system_error>

namespace pchess::engine {

EngineRegistry::Entry EngineRegistry::check(EngineDescriptor d) const {
    Entry e;
    std::error_code ec;
    const std::filesystem::path bin = d.binaryPath(platform_);
    if (bin.empty()) {
        e.problem = "no binary for platform " + platform_;
    } else if (!std::filesystem::is_regular_file(bin, ec)) {
        e.problem = "binary not found: " + bin.string();
    } else {
        for (const auto& f : d.files) {
            const std::filesystem::path p = d.dir / f;
            if (!std::filesystem::is_regular_file(p, ec)) {
                e.problem = "file not found: " + p.string();
                break;
            }
        }
    }
    e.available = e.problem.empty();
    e.descriptor = std::move(d);
    return e;
}

void EngineRegistry::scan(const std::filesystem::path& enginesDir, const std::string& platform) {
    platform_ = platform;
    entries_.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(enginesDir, ec)) {
        util::Log::info("engine", "no engines folder: " + enginesDir.string());
        return;
    }
    for (const auto& de : std::filesystem::directory_iterator(enginesDir, ec)) {
        if (!de.is_directory(ec)) continue;
        const std::filesystem::path yaml = de.path() / "engine.yaml";
        if (!std::filesystem::is_regular_file(yaml, ec)) continue;
        auto d = loadDescriptor(yaml);
        if (!d) {
            Entry broken;
            broken.descriptor.id = de.path().filename().string();
            broken.descriptor.name = broken.descriptor.id;
            broken.descriptor.dir = de.path();
            broken.problem = d.error().context;
            entries_.push_back(std::move(broken));
            continue;
        }
        if (find(d->id)) {
            util::Log::warn("engine", "duplicate engine id '" + d->id + "' in " + de.path().string() + ", ignored");
            continue;
        }
        entries_.push_back(check(std::move(*d)));
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.descriptor.id < b.descriptor.id; });
    for (const auto& e : entries_)
        util::Log::info("engine", e.descriptor.id + (e.available ? ": available (" + e.descriptor.name + ")"
                                                                 : ": disabled - " + e.problem));
}

void EngineRegistry::add(EngineDescriptor d, const std::string& platform) {
    platform_ = platform;
    Entry e = check(std::move(d));
    for (auto& existing : entries_)
        if (existing.descriptor.id == e.descriptor.id) {
            existing = std::move(e);
            return;
        }
    entries_.push_back(std::move(e));
}

const EngineRegistry::Entry* EngineRegistry::find(std::string_view id) const {
    for (const auto& e : entries_)
        if (e.descriptor.id == id) return &e;
    return nullptr;
}

const EngineDescriptor* EngineRegistry::available(std::string_view id) const {
    const Entry* e = find(id);
    return e && e->available ? &e->descriptor : nullptr;
}

std::vector<const EngineDescriptor*> EngineRegistry::availableFor(Role role) const {
    std::vector<const EngineDescriptor*> out;
    for (const auto& e : entries_)
        if (e.available && e.descriptor.supports(role)) out.push_back(&e.descriptor);
    return out;
}

const EngineDescriptor* EngineRegistry::lightestFor(Role role) const {
    const EngineDescriptor* best = nullptr;
    for (const EngineDescriptor* d : availableFor(role))
        if (!best || d->resources.energy < best->resources.energy) best = d;
    return best;
}

} // namespace pchess::engine
