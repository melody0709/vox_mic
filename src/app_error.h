#pragma once

// Error type for the non-realtime boundaries: config, ADB, the socket pipe.
//
// AGENTS.md asks those paths to report through std::expected<T, AppError>
// instead of bool plus an out-parameter. The out-parameter version loses the
// reason at the point where it matters: callers end up printing "failed"
// without being able to say why.
//
// Kept in its own header, away from app_state.h, on purpose: AppError owns a
// std::string, so constructing one allocates. That is fine on the config, ADB
// and pipe paths and forbidden on the WASAPI render thread - keeping the type
// out of the shared state header makes that boundary visible in the includes.

#include <expected>
#include <string>
#include <utility>

enum class AppErrorCode {
    none = 0,
    notFound,          // a required file, device or key is absent
    invalidArgument,   // a value failed validation
    io,                // a read or write failed
    unavailable,       // a dependency is missing or refused to start
    protocol,          // the peer broke the expected contract
};

struct AppError {
    AppErrorCode code = AppErrorCode::none;
    std::string detail;

    static AppError make(AppErrorCode code, std::string detail) {
        AppError error;
        error.code = code;
        error.detail = std::move(detail);
        return error;
    }
};

inline const char* toString(AppErrorCode code) {
    switch (code) {
    case AppErrorCode::none:            return "none";
    case AppErrorCode::notFound:        return "notFound";
    case AppErrorCode::invalidArgument: return "invalidArgument";
    case AppErrorCode::io:              return "io";
    case AppErrorCode::unavailable:     return "unavailable";
    case AppErrorCode::protocol:        return "protocol";
    }
    return "unknown";
}

// "code: detail", for logs and dialog text.
inline std::string describe(const AppError& error) {
    std::string text = toString(error.code);
    if (!error.detail.empty()) {
        text += ": ";
        text += error.detail;
    }
    return text;
}
