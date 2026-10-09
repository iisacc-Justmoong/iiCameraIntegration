#include "NativeCamera.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>

#if TARGET_OS_OSX || TARGET_OS_IOS || TARGET_OS_TV

// These APIs do not exist on macOS; Mac Catalyst uses the iOS API surface.
#if TARGET_OS_IOS || TARGET_OS_TV
#define IICAMERA_MOBILE_OPTICS 1
#else
#define IICAMERA_MOBILE_OPTICS 0
#endif

#if IICAMERA_MOBILE_OPTICS && (__IPHONE_OS_VERSION_MAX_ALLOWED >= 270000 || __TV_OS_VERSION_MAX_ALLOWED >= 270000)
#define IICAMERA_VARIABLE_APERTURE 1
#else
#define IICAMERA_VARIABLE_APERTURE 0
#endif

namespace iiCameraIntergration {
namespace {
std::mutex &nativeMutex() {
    static std::mutex value;
    return value;
}

std::string utf8(NSString *value) {
    const char *text = value.UTF8String;
    return text ? std::string(text) : std::string{};
}

std::string fourCC(OSType value) {
    std::string result(4, '\0');
    for (unsigned index = 0; index != 4; ++index)
        result[index] = static_cast<char>((value >> ((3 - index) * 8)) & 0xff);
    if (std::all_of(result.begin(), result.end(), [](unsigned char c) { return c >= 32 && c <= 126; }))
        return result;
    std::ostringstream text;
    text << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return text.str();
}

Error nativeError(NSError *error, ErrorCode code = ErrorCode::BackendFailure) {
    return {code, error ? utf8(error.localizedDescription) : "AVFoundation operation failed",
            error ? static_cast<int>(error.code) : 0};
}

Error exceptionError(NSException *exception) {
    return {ErrorCode::BackendFailure, utf8(exception.name) + ": " + utf8(exception.reason)};
}

CameraPermission permission() noexcept {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusNotDetermined: return CameraPermission::NotDetermined;
    case AVAuthorizationStatusRestricted: return CameraPermission::Restricted;
    case AVAuthorizationStatusDenied: return CameraPermission::Denied;
    case AVAuthorizationStatusAuthorized: return CameraPermission::Authorized;
    }
    return CameraPermission::Unavailable;
}

NSArray<AVCaptureDevice *> *discoverDevices() {
    NSMutableArray<AVCaptureDeviceType> *types = [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
#if TARGET_OS_OSX
    if (@available(macOS 14.0, *)) {
        [types addObject:AVCaptureDeviceTypeExternal];
        [types addObject:AVCaptureDeviceTypeContinuityCamera];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
    }
    if (@available(macOS 13.0, *)) [types addObject:AVCaptureDeviceTypeDeskViewCamera];
#else
    [types addObject:AVCaptureDeviceTypeBuiltInTelephotoCamera];
    [types addObject:AVCaptureDeviceTypeBuiltInDualCamera];
    [types addObject:AVCaptureDeviceTypeBuiltInTrueDepthCamera];
    if (@available(iOS 13.0, macCatalyst 14.0, tvOS 17.0, *)) {
        [types addObject:AVCaptureDeviceTypeBuiltInUltraWideCamera];
        [types addObject:AVCaptureDeviceTypeBuiltInDualWideCamera];
        [types addObject:AVCaptureDeviceTypeBuiltInTripleCamera];
    }
    if (@available(iOS 15.4, macCatalyst 15.4, tvOS 17.0, *))
        [types addObject:AVCaptureDeviceTypeBuiltInLiDARDepthCamera];
    if (@available(iOS 17.0, macCatalyst 17.0, tvOS 17.0, *)) {
        [types addObject:AVCaptureDeviceTypeExternal];
        [types addObject:AVCaptureDeviceTypeContinuityCamera];
    }
#endif
    return [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                                  mediaType:AVMediaTypeVideo
                                                                   position:AVCaptureDevicePositionUnspecified].devices;
}

#if IICAMERA_MOBILE_OPTICS
ControlCapability numeric(ControlId id, std::string unit, double minimum, double maximum, bool writable) {
    ControlCapability value;
    value.id = id;
    value.kind = ControlKind::Numeric;
    value.unit = std::move(unit);
    value.readable = true;
    if (std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum)
        value.range = NumericRange{minimum, maximum, 0, {}};
    value.writable = writable && value.range.has_value();
    value.additional["AVFoundation.application"] = "accepted asynchronously; query for observed device state";
    return value;
}
#endif

std::vector<ControlCapability> capabilities(AVCaptureDevice *device) {
    std::vector<ControlCapability> result;
    ControlCapability focus;
    focus.id = ControlId::FocusMode;
    focus.kind = ControlKind::Mode;
    focus.readable = true;
    for (auto [mode, native] : {std::pair{ControlMode::Locked, AVCaptureFocusModeLocked},
                              {ControlMode::Automatic, AVCaptureFocusModeAutoFocus},
                              {ControlMode::Continuous, AVCaptureFocusModeContinuousAutoFocus}})
        if ([device isFocusModeSupported:native]) focus.modes.push_back(mode);
    focus.writable = !focus.modes.empty();
    if (!focus.modes.empty()) result.push_back(std::move(focus));

    ControlCapability exposure;
    exposure.id = ControlId::ExposureMode;
    exposure.kind = ControlKind::Mode;
    exposure.readable = true;
    for (auto [mode, native] : {std::pair{ControlMode::Locked, AVCaptureExposureModeLocked},
                              {ControlMode::Automatic, AVCaptureExposureModeAutoExpose},
                              {ControlMode::Continuous, AVCaptureExposureModeContinuousAutoExposure}})
        if ([device isExposureModeSupported:native]) exposure.modes.push_back(mode);
#if IICAMERA_MOBILE_OPTICS
    if ([device isExposureModeSupported:AVCaptureExposureModeCustom]) exposure.modes.push_back(ControlMode::Manual);
#endif
    exposure.writable = !exposure.modes.empty();
    if (!exposure.modes.empty()) result.push_back(std::move(exposure));

    ControlCapability whiteBalance;
    whiteBalance.id = ControlId::WhiteBalanceMode;
    whiteBalance.kind = ControlKind::Mode;
    whiteBalance.readable = true;
    for (auto [mode, native] : {std::pair{ControlMode::Locked, AVCaptureWhiteBalanceModeLocked},
                              {ControlMode::Automatic, AVCaptureWhiteBalanceModeAutoWhiteBalance},
                              {ControlMode::Continuous, AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance}})
        if ([device isWhiteBalanceModeSupported:native]) whiteBalance.modes.push_back(mode);
    whiteBalance.writable = !whiteBalance.modes.empty();
    if (!whiteBalance.modes.empty()) result.push_back(std::move(whiteBalance));

#if IICAMERA_MOBILE_OPTICS
    const auto format = device.activeFormat;
    const auto minimumZoom = device.minAvailableVideoZoomFactor;
    const auto maximumZoom = device.maxAvailableVideoZoomFactor;
    if (minimumZoom > 0 && maximumZoom >= minimumZoom)
        result.push_back(numeric(ControlId::ZoomFactor, "factor", minimumZoom, maximumZoom, maximumZoom > minimumZoom));
    auto lensPosition = numeric(ControlId::LensPosition, "normalized position", 0, 1,
                                device.isLockingFocusWithCustomLensPositionSupported);
    lensPosition.additional["AVFoundation.coordinate"] = "unitless; not focus distance or infinity";
    result.push_back(std::move(lensPosition));
    if (std::isfinite(device.lensAperture) && device.lensAperture > 0) {
        auto aperture = numeric(ControlId::ApertureFNumber, "f-number", device.lensAperture, device.lensAperture, false);
#if IICAMERA_VARIABLE_APERTURE
        if (@available(iOS 27.0, macCatalyst 27.0, tvOS 27.0, *)) {
            if (format.minLensAperture > 0 && format.maxLensAperture >= format.minLensAperture) {
                aperture.range = NumericRange{format.minLensAperture, format.maxLensAperture, 0, {}};
                aperture.writable = format.maxLensAperture > format.minLensAperture &&
                    [format supportsExposureModeCustomWithLensAperture:AVCaptureLensApertureCurrent
                                                              duration:AVCaptureExposureDurationCurrent
                                                                   ISO:AVCaptureISOCurrent];
                std::string stops;
                for (NSNumber *stop in format.recommendedLensApertureStops) {
                    if (!stops.empty()) stops += ",";
                    stops += utf8(stop.stringValue);
                }
                aperture.additional["AVFoundation.recommendedApertureStops"] = std::move(stops);
            }
        }
#endif
        result.push_back(std::move(aperture));
    }
    if ([device isExposureModeSupported:AVCaptureExposureModeCustom]) {
        result.push_back(numeric(ControlId::ExposureSeconds, "seconds", CMTimeGetSeconds(format.minExposureDuration),
                                 CMTimeGetSeconds(format.maxExposureDuration), true));
        result.push_back(numeric(ControlId::Iso, "ISO", format.minISO, format.maxISO, true));
    }
    if (device.minExposureTargetBias <= device.maxExposureTargetBias)
        result.push_back(numeric(ControlId::ExposureCompensationEv, "EV", device.minExposureTargetBias,
                                 device.maxExposureTargetBias, device.maxExposureTargetBias > device.minExposureTargetBias));
    const double fieldOfView = format.videoFieldOfView;
    if (std::isfinite(fieldOfView) && fieldOfView > 0 && fieldOfView < 180) {
        auto value = numeric(ControlId::HorizontalFieldOfViewDegrees, "degrees", fieldOfView, fieldOfView, false);
        value.additional["AVFoundation.scope"] = "active format at native zoom; crop/distortion settings may change effective view";
        result.push_back(std::move(value));
    }
#endif
    return result;
}

Result<ControlValue> readControl(AVCaptureDevice *device, ControlId id) {
    switch (id) {
    case ControlId::FocusMode:
        switch (device.focusMode) {
        case AVCaptureFocusModeLocked: return ControlMode::Locked;
        case AVCaptureFocusModeAutoFocus: return ControlMode::Automatic;
        case AVCaptureFocusModeContinuousAutoFocus: return ControlMode::Continuous;
        }
        break;
    case ControlId::ExposureMode:
        switch (device.exposureMode) {
        case AVCaptureExposureModeLocked: return ControlMode::Locked;
        case AVCaptureExposureModeAutoExpose: return ControlMode::Automatic;
        case AVCaptureExposureModeContinuousAutoExposure: return ControlMode::Continuous;
        case AVCaptureExposureModeCustom: return ControlMode::Manual;
        }
        break;
    case ControlId::WhiteBalanceMode:
        switch (device.whiteBalanceMode) {
        case AVCaptureWhiteBalanceModeLocked: return ControlMode::Locked;
        case AVCaptureWhiteBalanceModeAutoWhiteBalance: return ControlMode::Automatic;
        case AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance: return ControlMode::Continuous;
        }
        break;
#if IICAMERA_MOBILE_OPTICS
    case ControlId::ZoomFactor: return static_cast<double>(device.videoZoomFactor);
    case ControlId::LensPosition: return static_cast<double>(device.lensPosition);
    case ControlId::ApertureFNumber: return static_cast<double>(device.lensAperture);
    case ControlId::ExposureSeconds: return CMTimeGetSeconds(device.exposureDuration);
    case ControlId::Iso: return static_cast<double>(device.ISO);
    case ControlId::ExposureCompensationEv: return static_cast<double>(device.exposureTargetBias);
    case ControlId::HorizontalFieldOfViewDegrees: return static_cast<double>(device.activeFormat.videoFieldOfView);
#endif
    default: break;
    }
    return std::unexpected(Error{ErrorCode::Unsupported, "The device does not expose this camera control"});
}

class ConfigurationLock final {
public:
    explicit ConfigurationLock(AVCaptureDevice *device) : device_(device) {}
    ~ConfigurationLock() { [device_ unlockForConfiguration]; }
private:
    AVCaptureDevice *__strong device_;
};

struct DeviceState {
    explicit DeviceState(AVCaptureDevice *value) : device(value) {}
    AVCaptureDevice *__strong device;
};

Result<ControlValue> applyControl(AVCaptureDevice *device, const ControlCommand &command) {
    if (permission() != CameraPermission::Authorized)
        return std::unexpected(Error{ErrorCode::PermissionDenied, "Camera authorization is required; the host application must request it"});
    const auto current = capabilities(device);
    const auto found = std::find_if(current.begin(), current.end(), [&](const auto &cap) { return cap.id == command.id; });
    if (found == current.end() || !found->writable)
        return std::unexpected(Error{ErrorCode::Unsupported, "This camera control is unavailable or read-only in the active format"});
    if (found->kind == ControlKind::Numeric) {
        const double *value = std::get_if<double>(&command.value);
        if (!value || !std::isfinite(*value) || !found->range || !found->range->contains(*value))
            return std::unexpected(Error{ErrorCode::InvalidArgument, "The value is outside the camera's current supported range"});
    } else {
        const auto *mode = std::get_if<ControlMode>(&command.value);
        if (!mode || std::find(found->modes.begin(), found->modes.end(), *mode) == found->modes.end())
            return std::unexpected(Error{ErrorCode::InvalidArgument, "The mode is unsupported by the camera"});
    }
    NSError *error = nil;
    if (![device lockForConfiguration:&error]) return std::unexpected(nativeError(error, ErrorCode::Busy));
    ConfigurationLock lock(device);
    if (!device.isConnected)
        return std::unexpected(Error{ErrorCode::NotFound, "The camera was disconnected while acquiring the device lock"});
    // The active format can change between capability discovery and the lock.
    // Revalidate numeric bounds under exclusive configuration access.
    if (found->kind == ControlKind::Numeric) {
        const auto lockedCapabilities = capabilities(device);
        const auto locked = std::find_if(lockedCapabilities.begin(), lockedCapabilities.end(),
                                        [&](const auto &cap) { return cap.id == command.id; });
        if (locked == lockedCapabilities.end() || !locked->writable)
            return std::unexpected(Error{ErrorCode::Unsupported, "The control became unavailable while acquiring the device lock"});
        if (!locked->range || !locked->range->contains(std::get<double>(command.value)))
            return std::unexpected(Error{ErrorCode::InvalidArgument, "The value is outside the camera's newly active format range"});
    }
    switch (command.id) {
    case ControlId::FocusMode: {
        const auto mode = std::get<ControlMode>(command.value);
        const auto native = mode == ControlMode::Locked ? AVCaptureFocusModeLocked :
                            mode == ControlMode::Automatic ? AVCaptureFocusModeAutoFocus : AVCaptureFocusModeContinuousAutoFocus;
        if (![device isFocusModeSupported:native]) return std::unexpected(Error{ErrorCode::Unsupported, "Focus mode changed while acquiring the device lock"});
        device.focusMode = native;
        break;
    }
    case ControlId::ExposureMode: {
        const auto mode = std::get<ControlMode>(command.value);
        const auto native = mode == ControlMode::Locked ? AVCaptureExposureModeLocked :
                            mode == ControlMode::Automatic ? AVCaptureExposureModeAutoExpose :
                            mode == ControlMode::Manual ? AVCaptureExposureModeCustom : AVCaptureExposureModeContinuousAutoExposure;
        if (![device isExposureModeSupported:native]) return std::unexpected(Error{ErrorCode::Unsupported, "Exposure mode changed while acquiring the device lock"});
        device.exposureMode = native;
        break;
    }
    case ControlId::WhiteBalanceMode: {
        const auto mode = std::get<ControlMode>(command.value);
        const auto native = mode == ControlMode::Locked ? AVCaptureWhiteBalanceModeLocked :
                            mode == ControlMode::Automatic ? AVCaptureWhiteBalanceModeAutoWhiteBalance : AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance;
        if (![device isWhiteBalanceModeSupported:native]) return std::unexpected(Error{ErrorCode::Unsupported, "White-balance mode changed while acquiring the device lock"});
        device.whiteBalanceMode = native;
        break;
    }
#if IICAMERA_MOBILE_OPTICS
    case ControlId::ZoomFactor:
        device.videoZoomFactor = std::get<double>(command.value);
        break;
    case ControlId::LensPosition:
        if (!device.isLockingFocusWithCustomLensPositionSupported)
            return std::unexpected(Error{ErrorCode::Unsupported, "Manual lens position is no longer supported"});
        [device setFocusModeLockedWithLensPosition:static_cast<float>(std::get<double>(command.value)) completionHandler:nil];
        break;
    case ControlId::ExposureSeconds:
        [device setExposureModeCustomWithDuration:CMTimeMakeWithSeconds(std::get<double>(command.value), 1000000000)
                                              ISO:AVCaptureISOCurrent completionHandler:nil];
        break;
    case ControlId::Iso:
        [device setExposureModeCustomWithDuration:AVCaptureExposureDurationCurrent
                                              ISO:static_cast<float>(std::get<double>(command.value)) completionHandler:nil];
        break;
    case ControlId::ExposureCompensationEv:
        [device setExposureTargetBias:static_cast<float>(std::get<double>(command.value)) completionHandler:nil];
        break;
#if IICAMERA_VARIABLE_APERTURE
    case ControlId::ApertureFNumber:
        if (@available(iOS 27.0, macCatalyst 27.0, tvOS 27.0, *)) {
            const float aperture = static_cast<float>(std::get<double>(command.value));
            if (![device.activeFormat supportsExposureModeCustomWithLensAperture:aperture
                                                                           duration:AVCaptureExposureDurationCurrent ISO:AVCaptureISOCurrent])
                return std::unexpected(Error{ErrorCode::Unsupported, "The active format does not support this aperture/exposure combination"});
            [device setExposureModeCustomWithLensAperture:aperture duration:AVCaptureExposureDurationCurrent
                                                     ISO:AVCaptureISOCurrent completionHandler:nil];
            break;
        }
        [[fallthrough]];
#endif
#endif
    default:
        return std::unexpected(Error{ErrorCode::Unsupported, "This camera control is not implemented by AVFoundation"});
    }
    // Returns the accepted value, because numerical focus/exposure updates are
    // asynchronous. It must not be presented as an observed captured-frame state.
    return command.value;
}

CameraInfo describe(AVCaptureDevice *device) {
    CameraInfo value;
    value.id = utf8(device.uniqueID);
    value.name = utf8(device.localizedName);
    value.manufacturer = utf8(device.manufacturer);
    value.model = utf8(device.modelID);
    value.backend = "AVFoundation";
    value.connected = device.isConnected;
    value.position = device.position == AVCaptureDevicePositionFront ? Position::Front :
                     device.position == AVCaptureDevicePositionBack ? Position::Back : Position::Unknown;
    value.additional["AVFoundation.deviceType"] = utf8(device.deviceType);
    value.additional["AVFoundation.suspended"] = device.isSuspended ? "true" : "false";
    value.additional["AVFoundation.metadataScope"] = "OS-reported; unknown sensor/lens/firmware/serial fields omitted";
#if TARGET_OS_OSX
    value.additional["AVFoundation.transportFourCC"] = fourCC(static_cast<OSType>(device.transportType));
    value.additional["AVFoundation.inUseByAnotherApplication"] = device.isInUseByAnotherApplication ? "true" : "false";
    switch (static_cast<OSType>(device.transportType)) {
    case 'usb ': value.transport = Transport::Usb; break;
    case 'bltn': value.transport = Transport::BuiltIn; break;
    case 'virt': value.transport = Transport::Virtual; break;
    default: break;
    }
    if (@available(macOS 14.0, *)) {
        if ([device.deviceType isEqualToString:AVCaptureDeviceTypeExternal]) value.position = Position::External;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if ([device.deviceType isEqualToString:AVCaptureDeviceTypeExternalUnknown]) value.position = Position::External;
#pragma clang diagnostic pop
    }
#else
    if (@available(iOS 17.0, macCatalyst 17.0, tvOS 17.0, *)) {
        if ([device.deviceType isEqualToString:AVCaptureDeviceTypeExternal]) value.position = Position::External;
    }
    if (value.position == Position::Front || value.position == Position::Back) value.transport = Transport::BuiltIn;
    value.additional["AVFoundation.virtualDevice"] = device.isVirtualDevice ? "true" : "false";
    std::string constituents;
    for (AVCaptureDevice *constituent in device.constituentDevices) {
        if (!constituents.empty()) constituents += ",";
        constituents += utf8(constituent.uniqueID);
    }
    value.additional["AVFoundation.constituentDeviceIds"] = std::move(constituents);
#endif
    std::size_t index = 0;
    for (AVCaptureDeviceFormat *native in device.formats) {
        CameraFormat format;
        format.id = "AVFoundation.format." + std::to_string(index++);
        const auto description = native.formatDescription;
        const auto dimensions = CMVideoFormatDescriptionGetDimensions(description);
        if (dimensions.width <= 0 || dimensions.height <= 0) continue;
        format.resolution = {static_cast<std::uint32_t>(dimensions.width), static_cast<std::uint32_t>(dimensions.height)};
        const auto subtype = CMFormatDescriptionGetMediaSubType(description);
        const auto nativeTag = fourCC(subtype);
        NSDictionary *pixelDescription = CFBridgingRelease(CVPixelFormatDescriptionCreateWithPixelFormatType(kCFAllocatorDefault, subtype));
        if (pixelDescription) {
            format.pixelFormat = nativeTag;
            format.additional["CoreMedia.subtypeKind"] = "registered pixel format";
            format.additional["CoreVideo.pixelFormatDescription"] = utf8(pixelDescription.description);
        } else {
            format.additional["CoreMedia.subtypeKind"] = "unclassified native video subtype";
        }
        format.additional["CoreMedia.mediaType"] = fourCC(CMFormatDescriptionGetMediaType(description));
        format.additional["CoreMedia.mediaSubtype"] = nativeTag;
        format.additional["CoreMedia.formatExtensions"] = utf8([(__bridge NSDictionary *)CMFormatDescriptionGetExtensions(description) description]);
        format.additional["AVFoundation.description"] = utf8(native.description);
        for (AVFrameRateRange *range in native.videoSupportedFrameRateRanges) {
            if (std::isfinite(range.minFrameRate) && std::isfinite(range.maxFrameRate) && range.minFrameRate > 0 && range.maxFrameRate >= range.minFrameRate)
                format.frameRates.push_back({range.minFrameRate, range.maxFrameRate, 0, {}});
        }
#if IICAMERA_MOBILE_OPTICS
        if (std::isfinite(native.videoFieldOfView) && native.videoFieldOfView > 0 && native.videoFieldOfView < 180)
            format.horizontalFieldOfViewDegrees = native.videoFieldOfView;
        format.hdrSupported = native.isVideoHDRSupported;
        format.additional["AVFoundation.hdrMeaning"] = "EDR streaming toggle; not a complete HDR color-space capability";
#endif
        if (native == device.activeFormat) value.activeFormatId = format.id;
        value.formats.push_back(std::move(format));
    }
    value.controls = capabilities(device);
#if IICAMERA_MOBILE_OPTICS
    LensInfo lens;
    lens.id = "AVFoundation.activeLens";
    lens.additional["AVFoundation.metadataScope"] = "active lens of device; physical lens model and focal length unknown";
    if (device.lensAperture > 0 && std::isfinite(device.lensAperture)) lens.currentApertureFNumber = device.lensAperture;
    if (device.activeFormat.videoFieldOfView > 0 && device.activeFormat.videoFieldOfView < 180)
        lens.horizontalFieldOfViewDegrees = device.activeFormat.videoFieldOfView;
    const auto aperture = std::find_if(value.controls.begin(), value.controls.end(), [](const auto &cap) { return cap.id == ControlId::ApertureFNumber; });
    if (aperture != value.controls.end()) lens.apertureFNumber = aperture->range;
    if (lens.currentApertureFNumber && lens.apertureFNumber && !lens.apertureFNumber->contains(*lens.currentApertureFNumber)) {
        // OS auto exposure can adjust the physical aperture between reads.
        // Preserve that observation without declaring an inconsistent range.
        lens.additional["AVFoundation.observedApertureFNumber"] = std::to_string(*lens.currentApertureFNumber);
        lens.currentApertureFNumber.reset();
    }
    value.lenses.push_back(std::move(lens));
#endif
    return value;
}
} // namespace

bool NativeCameraSystem::available() const noexcept { return true; }

CameraPermission NativeCameraSystem::permissionStatus() const noexcept {
    @autoreleasepool { return permission(); }
}

Result<std::vector<CameraInfo>> NativeCameraSystem::cameras() const {
    @autoreleasepool {
        std::lock_guard guard(nativeMutex());
        @try {
            std::vector<CameraInfo> result;
            for (AVCaptureDevice *device in discoverDevices()) result.push_back(describe(device));
            return result;
        } @catch (NSException *exception) {
            return std::unexpected(exceptionError(exception));
        }
    }
}

Result<ControlPort> NativeCameraSystem::controls(const std::string &deviceId) const {
    if (deviceId.empty() || deviceId.find('\0') != std::string::npos)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "A nonempty camera device ID is required"});
    @autoreleasepool {
        std::lock_guard guard(nativeMutex());
        @try {
            NSString *identifier = [[NSString alloc] initWithBytes:deviceId.data() length:deviceId.size() encoding:NSUTF8StringEncoding];
            if (!identifier) return std::unexpected(Error{ErrorCode::InvalidArgument, "The camera device ID must be valid UTF-8"});
            AVCaptureDevice *device = [AVCaptureDevice deviceWithUniqueID:identifier];
            if (!device || !device.isConnected || ![device hasMediaType:AVMediaTypeVideo])
                return std::unexpected(Error{ErrorCode::NotFound, "The requested camera is not connected"});
            auto state = std::make_shared<DeviceState>(device);
            ControlCallbacks callbacks;
            callbacks.query = [state](ControlId id) -> Result<ControlValue> {
                @autoreleasepool {
                    std::lock_guard guard(nativeMutex());
                    @try {
                        if (!state->device.isConnected) return std::unexpected(Error{ErrorCode::NotFound, "The camera was disconnected"});
                        const auto current = capabilities(state->device);
                        const auto supported = std::find_if(current.begin(), current.end(), [&](const auto &cap) { return cap.id == id && cap.readable; });
                        if (supported == current.end()) return std::unexpected(Error{ErrorCode::Unsupported, "The active format does not expose this control"});
                        return readControl(state->device, id);
                    } @catch (NSException *exception) { return std::unexpected(exceptionError(exception)); }
                }
            };
            callbacks.apply = [state](const ControlCommand &command) -> Result<ControlValue> {
                @autoreleasepool {
                    std::lock_guard guard(nativeMutex());
                    @try {
                        if (!state->device.isConnected) return std::unexpected(Error{ErrorCode::NotFound, "The camera was disconnected"});
                        return applyControl(state->device, command);
                    } @catch (NSException *exception) { return std::unexpected(exceptionError(exception)); }
                }
            };
            return ControlPort(capabilities(device), std::move(callbacks));
        } @catch (NSException *exception) { return std::unexpected(exceptionError(exception)); }
    }
}
} // namespace iiCameraIntergration

#else
#include "NativeCameraUnavailable.cpp"
#endif
