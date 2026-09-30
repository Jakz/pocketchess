// SPDX-License-Identifier: GPL-3.0-or-later
#include "tools/common/LineReader.h"

#include <zstd.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace pchess::tools {

namespace {

struct FileCloser {
    void operator()(std::FILE* f) const {
        if (f) std::fclose(f);
    }
};
struct DStreamFree {
    void operator()(ZSTD_DStream* d) const { ZSTD_freeDStream(d); }
};

// Splits a byte stream into lines, carrying a partial line between chunks.
class LineSplitter {
public:
    explicit LineSplitter(const std::function<bool(std::string_view)>& onLine) : onLine_(onLine) {}
    bool feed(const char* data, size_t size) {
        size_t start = 0;
        for (size_t i = 0; i < size; ++i) {
            if (data[i] != '\n') continue;
            if (!emit(data + start, i - start)) return false;
            start = i + 1;
        }
        carry_.append(data + start, size - start);
        return true;
    }
    bool finish() {
        if (carry_.empty()) return true;
        std::string last = std::move(carry_);
        carry_.clear();
        return deliver(last);
    }
    uint64_t lines() const { return lines_; }

private:
    bool emit(const char* data, size_t size) {
        if (carry_.empty()) return deliver(std::string_view(data, size));
        carry_.append(data, size);
        std::string line = std::move(carry_);
        carry_.clear();
        return deliver(line);
    }
    bool deliver(std::string_view line) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        ++lines_;
        return onLine_(line);
    }
    const std::function<bool(std::string_view)>& onLine_;
    std::string carry_;
    uint64_t lines_ = 0;
};

} // namespace

Result<uint64_t> forEachLine(const std::filesystem::path& file, const std::function<bool(std::string_view)>& onLine) {
    std::unique_ptr<std::FILE, FileCloser> f(std::fopen(file.string().c_str(), "rb"));
    if (!f) return makeError(ErrorCode::NotFound, file.string());
    LineSplitter lines(onLine);
    const bool zst = file.extension() == ".zst";
    std::vector<char> in(zst ? ZSTD_DStreamInSize() : (1u << 20));
    if (!zst) {
        for (;;) {
            const size_t n = std::fread(in.data(), 1, in.size(), f.get());
            if (n == 0) break;
            if (!lines.feed(in.data(), n)) return lines.lines();
        }
        if (std::ferror(f.get())) return makeError(ErrorCode::Io, file.string() + ": read error");
        lines.finish();
        return lines.lines();
    }
    std::unique_ptr<ZSTD_DStream, DStreamFree> ds(ZSTD_createDStream());
    if (!ds) return makeError(ErrorCode::Io, "zstd: out of memory");
    ZSTD_initDStream(ds.get());
    std::vector<char> out(ZSTD_DStreamOutSize());
    size_t last = 0;
    for (;;) {
        const size_t n = std::fread(in.data(), 1, in.size(), f.get());
        if (n == 0) break;
        ZSTD_inBuffer input{in.data(), n, 0};
        // Until the input is consumed and the decoder has nothing more to flush (a full output
        // buffer may hide more data even when all input was read).
        for (;;) {
            ZSTD_outBuffer output{out.data(), out.size(), 0};
            last = ZSTD_decompressStream(ds.get(), &output, &input);
            if (ZSTD_isError(last))
                return makeError(ErrorCode::CorruptData, file.string() + ": zstd: " + ZSTD_getErrorName(last));
            if (!lines.feed(out.data(), output.pos)) return lines.lines();
            if (input.pos == input.size && output.pos < output.size) break;
        }
    }
    if (std::ferror(f.get())) return makeError(ErrorCode::Io, file.string() + ": read error");
    if (last != 0) return makeError(ErrorCode::CorruptData, file.string() + ": truncated zstd stream");
    lines.finish();
    return lines.lines();
}

} // namespace pchess::tools
