#pragma once
#include <iiCameraIntergration/Camera.h>
#include <cstddef>
#include <string_view>

namespace iiCameraIntergration {
struct FeatureQueryOptions {
    // False returns capability metadata without reading current control values.
    bool includeCurrentValues{true};
};
enum class FeatureType { Integer, Number, Boolean, Menu, String, Bytes, Compound, Action, Group };
using FeatureValue = std::variant<std::int64_t,double,bool,std::string,
    std::vector<std::byte>,std::vector<std::int64_t>,std::vector<double>>;
struct FeatureChoice { std::string id, name; };
// Backend IDs/types preserve vendor controls that have no standard ControlId.
// writable describes driver support; only ControlPort exposes standard mutations.
struct NativeFeature {
    std::string id, name;
    FeatureType type{FeatureType::Integer};
    bool readable{}, writable{};
    std::optional<NumericRange> range;
    std::vector<FeatureChoice> choices;
    std::optional<Result<FeatureValue>> value;
    Metadata additional;
};
struct CameraFeatures {
    CameraInfo camera;
    std::vector<ControlReading> controls;
    std::vector<NativeFeature> nativeFeatures;
};
struct CameraFeatureResult {
    std::string deviceId;
    Result<CameraFeatures> features;
};
// Helper for native and callback providers. A control-port failure is retained
// for each readable control, leaving metadata and other cameras inspectable.
[[nodiscard]] IICAMERAINTERGRATION_EXPORT CameraFeatures queryCameraFeatures(
    CameraInfo camera, const Result<ControlPort>& controls,
    const FeatureQueryOptions& options = {});
[[nodiscard]] IICAMERAINTERGRATION_EXPORT std::string_view controlName(ControlId) noexcept;
} // namespace iiCameraIntergration
