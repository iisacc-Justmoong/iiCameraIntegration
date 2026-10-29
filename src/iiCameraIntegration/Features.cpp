#include <iiCameraIntegration/Features.h>

namespace iiCameraIntegration {
CameraFeatures queryCameraFeatures(CameraInfo camera, const Result<ControlPort>& port,
                                  const FeatureQueryOptions& options) {
    CameraFeatures result;
    result.camera=std::move(camera);
    if (port) result.camera.controls=port->capabilities();
    if (!options.includeCurrentValues) return result;
    if (port) result.controls=port->queryAll();
    else for (const auto& capability : result.camera.controls)
        result.controls.push_back({capability,std::unexpected(capability.readable ? port.error() :
            Error{ErrorCode::Unsupported,"Control is not readable"})});
    return result;
}
std::string_view controlName(ControlId id) noexcept {
    switch(id) {
#define IICAMERA_CONTROL_NAME(x) case ControlId::x: return #x;
    IICAMERA_CONTROL_NAME(Lens) IICAMERA_CONTROL_NAME(FocalLengthMm)
    IICAMERA_CONTROL_NAME(HorizontalFieldOfViewDegrees) IICAMERA_CONTROL_NAME(ApertureFNumber)
    IICAMERA_CONTROL_NAME(ZoomFactor) IICAMERA_CONTROL_NAME(FocusDistanceMeters)
    IICAMERA_CONTROL_NAME(LensPosition) IICAMERA_CONTROL_NAME(FocusMode)
    IICAMERA_CONTROL_NAME(ExposureSeconds) IICAMERA_CONTROL_NAME(Iso)
    IICAMERA_CONTROL_NAME(ExposureMode) IICAMERA_CONTROL_NAME(ExposureCompensationEv)
    IICAMERA_CONTROL_NAME(WhiteBalanceKelvin) IICAMERA_CONTROL_NAME(WhiteBalanceMode)
    IICAMERA_CONTROL_NAME(StabilizationMode) IICAMERA_CONTROL_NAME(Brightness)
    IICAMERA_CONTROL_NAME(Contrast) IICAMERA_CONTROL_NAME(Saturation) IICAMERA_CONTROL_NAME(Hue)
    IICAMERA_CONTROL_NAME(Sharpness) IICAMERA_CONTROL_NAME(Gamma) IICAMERA_CONTROL_NAME(Gain)
    IICAMERA_CONTROL_NAME(BacklightCompensation) IICAMERA_CONTROL_NAME(PowerLineFrequency)
    IICAMERA_CONTROL_NAME(PanDegrees) IICAMERA_CONTROL_NAME(TiltDegrees)
    IICAMERA_CONTROL_NAME(RollDegrees) IICAMERA_CONTROL_NAME(TorchMode)
#undef IICAMERA_CONTROL_NAME
    }
    return "Unknown";
}
} // namespace iiCameraIntegration
