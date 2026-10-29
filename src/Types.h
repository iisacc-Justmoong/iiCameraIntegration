#pragma once
#include <iiCameraIntegration/Export.h>
#include <cstdint>
#include <expected>
#include <map>
#include <string>

namespace iiCameraIntegration {
enum class ErrorCode {
    InvalidArgument, Unavailable, Unsupported, PermissionDenied, NotFound,
    Busy, Io, InvalidMedia, EndOfStream, Cancelled, Timeout, BackendFailure
};
struct Error {
    ErrorCode code{ErrorCode::BackendFailure};
    std::string message;
    int nativeCode{};
};
template<class T> using Result = std::expected<T, Error>;
using Metadata = std::map<std::string, std::string, std::less<>>;
struct Rational {
    std::int64_t numerator{};
    std::int64_t denominator{1};
    [[nodiscard]] double value() const noexcept {
        return denominator ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
    }
};
struct Size { std::uint32_t width{}, height{}; };
}
