// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/engine/Engine.h"
#include "core/engine/PowerProfile.h"
#include "core/util/Scheduler.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pchess::engine {

// Fixed benchmark positions (FEN), the same on every platform so desktop and device runs compare.
struct BenchPosition {
    const char* name;
    const char* fen;
};
const std::vector<BenchPosition>& benchmarkPositions();

// One engine x one strength label (or "analysis" = the profile's Quick analysis) over all positions.
struct BenchRow {
    std::string strength;
    int searches = 0;
    int failures = 0;
    double wallMs = 0;
    double cpuSeconds = 0;
    uint64_t nodes = 0;
    int depthSum = 0;

    double avgDepth() const { return searches > failures ? double(depthSum) / (searches - failures) : 0.0; }
    double knps() const { return wallMs > 0 ? double(nodes) / wallMs : 0.0; }  // nodes per ms = knps
};

struct BenchEngine {
    std::string id;
    std::string name;  // the engine's own "id name" (descriptor name if it never started)
    std::string error; // set when the engine could not be started
    int startupMs = 0; // spawn -> readyok of the first start
    uint64_t peakRssKb = 0;
    std::vector<BenchRow> rows;
};

struct BenchReport {
    int64_t unixSeconds = 0;
    std::string platform;
    std::string profile;
    int positions = 0;
    bool cancelled = false;
    std::vector<BenchEngine> engines;
};

// Writes the report as YAML (logs/engine-bench.yaml; readable by util::Yaml).
std::string toYaml(const BenchReport& r);

// The engine benchmark of M2 (Diagnostics -> Bench): every engine x every strength label of its
// descriptor, plus Quick analysis, on benchmarkPositions(). Levels without a bounded limit use
// the profile's play budget. Engines run one after another (one process at a time) with the
// profile's Threads/Hash/priority. Everything happens on the scheduler's thread.
class EngineBenchmark {
public:
    using EngineFactory = std::function<std::unique_ptr<IEngine>(const EngineDescriptor&, util::Scheduler&)>;
    using ProgressCallback = std::function<void(const std::string& what, int done, int total)>;
    using DoneCallback = std::function<void(const BenchReport&)>;

    EngineBenchmark(util::Scheduler& scheduler, std::vector<EngineDescriptor> engines, PowerProfileId profile,
                    EngineFactory factory = {});
    ~EngineBenchmark();
    EngineBenchmark(const EngineBenchmark&) = delete;
    EngineBenchmark& operator=(const EngineBenchmark&) = delete;

    // `onDone` fires exactly once (also after cancel()), from a posted task.
    void start(int64_t unixSeconds, const std::string& platform, ProgressCallback onProgress, DoneCallback onDone);
    void cancel();
    bool running() const { return running_; }
    int totalSearches() const;

private:
    struct Step {
        size_t row;
        size_t position;
    };
    void next();
    void startEngine();
    void finishEngine();
    void finish();
    void onResult(const SearchResult& r, const EngineStats& before);

    util::Scheduler& scheduler_;
    std::vector<EngineDescriptor> descriptors_;
    const PowerProfile& profile_;
    EngineFactory factory_;
    ProgressCallback onProgress_;
    DoneCallback onDone_;

    BenchReport report_;
    std::unique_ptr<IEngine> engine_;
    std::vector<std::unique_ptr<IEngine>> retired_;  // destroyed from a posted task
    std::vector<std::pair<std::string, OptionSet>> rowOptions_;
    std::vector<SearchLimits> rowLimits_;
    size_t engineIndex_ = 0;
    Step step_{0, 0};
    int done_ = 0;
    bool running_ = false;
    bool cancelled_ = false;
    std::shared_ptr<char> token_;
};

} // namespace pchess::engine
