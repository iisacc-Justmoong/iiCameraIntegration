#pragma once

#include <iiCameraIntergration/Camera.h>
#include <iiCameraIntergration/Devices.h>
#include <iiCameraIntergration/Export.h>

namespace iiCameraIntergration {

enum class CameraPermission {
    NotDetermined,
    Restricted,
    Denied,
    Authorized,
    Unavailable
};

/**
 * The host operating system's camera discovery and control bridge.
 *
 * Discovery does not open a capture session or request camera permission.
 * Applications own permission prompts and their privacy usage descriptions.
 * Control callbacks retain their device and recheck its current capabilities.
 */
class IICAMERAINTERGRATION_EXPORT NativeCameraSystem final {
public:
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] CameraPermission permissionStatus() const noexcept;
    [[nodiscard]] Result<std::vector<CameraInfo>> cameras() const;
    [[nodiscard]] Result<ControlPort> controls(const std::string &deviceId) const;
    [[nodiscard]] Result<CameraFeatures> features(const std::string& deviceId,
        const FeatureQueryOptions& options = {}) const;
    [[nodiscard]] Result<std::vector<CameraFeatureResult>> features(
        const FeatureQueryOptions& options = {}) const;
    [[nodiscard]] CameraDeviceSystem devices() const;
    // Owns a native device connection without starting capture or OS bonding.
    // Apple attaches input to a stopped session; Linux retains an FD; Windows
    // retains an unstarted media source; Android opens a Camera2 device.
    [[nodiscard]] Result<CameraPairing> pair(const std::string &deviceId,
        const PairingOptions& options = {}) const;
};

} // namespace iiCameraIntergration
