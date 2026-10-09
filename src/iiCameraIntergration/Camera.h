#pragma once
#include <iiCameraIntergration/Types.h>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace iiCameraIntergration {
enum class Position { Unknown, Front, Back, External };
enum class Transport { Unknown, BuiltIn, Usb, Network, Virtual };
enum class Projection { Rectilinear, Fisheye, Unknown };
struct NumericRange {
    double minimum{}, maximum{};
    // Zero means continuously variable; discreteValues, when present, is authoritative.
    double step{};
    std::vector<double> discreteValues;
    [[nodiscard]] IICAMERAINTERGRATION_EXPORT bool contains(double value) const noexcept;
};
struct SensorInfo {
    std::optional<double> widthMm, heightMm, cropFactor;
    std::optional<Size> resolution;
    std::optional<std::uint32_t> bitDepth;
    std::string colorFilterArray;
    Metadata additional;
};
struct LensInfo {
    std::string id, manufacturer, model, mount, serialNumber;
    Projection projection{Projection::Unknown};
    std::optional<NumericRange> focalLengthMm, apertureFNumber, focusDistanceMeters;
    std::optional<double> currentFocalLengthMm, currentApertureFNumber;
    std::optional<double> horizontalFieldOfViewDegrees;
    bool interchangeable{};
    Metadata additional;
};
struct CameraFormat {
    std::string id, pixelFormat, codec;
    Size resolution;
    std::vector<NumericRange> frameRates;
    std::optional<double> horizontalFieldOfViewDegrees;
    bool hdrSupported{};
    Metadata additional;
};
enum class ControlId {
    Lens, FocalLengthMm, HorizontalFieldOfViewDegrees, ApertureFNumber,
    ZoomFactor, FocusDistanceMeters, LensPosition, FocusMode,
    ExposureSeconds, Iso, ExposureMode, ExposureCompensationEv,
    WhiteBalanceKelvin, WhiteBalanceMode, StabilizationMode
};
enum class ControlMode { Manual, Automatic, Continuous, Locked, Off, On };
using ControlValue = std::variant<double, ControlMode, std::string>;
enum class ControlKind { Numeric, Mode, Identifier };
struct ControlCapability {
    ControlId id{};
    ControlKind kind{ControlKind::Numeric};
    std::string unit;
    bool readable{}, writable{};
    std::optional<NumericRange> range;
    std::vector<ControlMode> modes;
    std::vector<std::string> identifiers;
    Metadata additional;
};
struct CameraInfo {
    std::string id, name, manufacturer, model, serialNumber, firmwareVersion, backend;
    Position position{Position::Unknown};
    Transport transport{Transport::Unknown};
    bool connected{true};
    std::optional<SensorInfo> sensor;
    std::vector<LensInfo> lenses;
    std::vector<CameraFormat> formats;
    std::optional<std::string> activeFormatId;
    std::vector<ControlCapability> controls;
    // Native/provider fields not expressible above are retained here, namespaced by provider.
    Metadata additional;
};
struct ControlCommand { ControlId id; ControlValue value; };
struct ControlCallbacks {
    std::function<Result<ControlValue>(ControlId)> query;
    std::function<Result<ControlValue>(const ControlCommand&)> apply;
};
// Retains the provider via callbacks; no mutable metadata escapes this value instance.
// Capability snapshots are preflight checks. Providers must revalidate live hardware state.
class IICAMERAINTERGRATION_EXPORT ControlPort final {
public:
    ControlPort(std::vector<ControlCapability> capabilities, ControlCallbacks callbacks);
    [[nodiscard]] const std::vector<ControlCapability>& capabilities() const noexcept;
    [[nodiscard]] Result<ControlValue> query(ControlId id) const;
    [[nodiscard]] Result<ControlValue> apply(const ControlCommand& command) const;
private:
    std::vector<ControlCapability> capabilities_;
    ControlCallbacks callbacks_;
};
struct FieldOfView { double horizontalDegrees{}, verticalDegrees{}, diagonalDegrees{}; };
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<FieldOfView> rectilinearFieldOfView(
    double sensorWidthMm, double sensorHeightMm, double focalLengthMm);
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<double> focalLengthForHorizontalFieldOfView(
    double sensorWidthMm, double horizontalDegrees);
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<void> validateCameraInfo(const CameraInfo& camera);
}
