// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Error.h"

namespace pchess {

const char* toString(ErrorCode code) {
    switch (code) {
    case ErrorCode::InvalidArgument: return "invalid argument";
    case ErrorCode::ParseFen: return "invalid FEN";
    case ErrorCode::ParseSan: return "invalid SAN";
    case ErrorCode::ParsePgn: return "invalid PGN";
    case ErrorCode::ParseUci: return "invalid UCI";
    case ErrorCode::IllegalMove: return "illegal move";
    case ErrorCode::CorruptData: return "corrupt data";
    case ErrorCode::NotFound: return "not found";
    case ErrorCode::Io: return "I/O error";
    case ErrorCode::EngineMissing: return "engine missing";
    case ErrorCode::EngineFailed: return "engine failed";
    case ErrorCode::Unsupported: return "unsupported";
    }
    return "unknown error";
}

} // namespace pchess
