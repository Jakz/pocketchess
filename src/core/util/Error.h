// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace pchess {

// Typed error codes (design notes §32). Screens map codes to remedies.
enum class ErrorCode {
    InvalidArgument,
    ParseFen,
    ParseSan,
    ParsePgn,
    ParseUci,
    IllegalMove,
    CorruptData,
    NotFound,
    Io,
    EngineMissing,
    EngineFailed,
    Unsupported,
};

const char* toString(ErrorCode code);

struct Error {
    ErrorCode code = ErrorCode::InvalidArgument;
    std::string context;

    std::string message() const { return std::string(toString(code)) + ": " + context; }
};

// Minimal expected-like result type (C++20; std::expected is C++23).
template <class T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error error) : v_(std::move(error)) {}

    bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }

    T& value() & { assert(ok()); return std::get<0>(v_); }
    const T& value() const& { assert(ok()); return std::get<0>(v_); }
    T&& value() && { assert(ok()); return std::get<0>(std::move(v_)); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

    const Error& error() const { assert(!ok()); return std::get<1>(v_); }

private:
    std::variant<T, Error> v_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), ok_(false) {}
    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { assert(!ok_); return error_; }

private:
    Error error_{};
    bool ok_ = true;
};

inline Error makeError(ErrorCode code, std::string context) { return Error{code, std::move(context)}; }

} // namespace pchess
