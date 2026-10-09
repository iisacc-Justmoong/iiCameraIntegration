#include "NativeCamera.h"

namespace iiCameraIntergration {
bool NativeCameraSystem::available() const noexcept { return false; }
CameraPermission NativeCameraSystem::permissionStatus() const noexcept { return CameraPermission::Unavailable; }
Result<std::vector<CameraInfo>> NativeCameraSystem::cameras() const {
    return std::unexpected(Error{ErrorCode::Unavailable, "No native camera discovery adapter is installed for this platform"});
}
Result<ControlPort> NativeCameraSystem::controls(const std::string &) const {
    return std::unexpected(Error{ErrorCode::Unavailable, "No native camera control adapter is installed for this platform"});
}
Result<CameraPairing> NativeCameraSystem::pair(const std::string &, const PairingOptions &) const {
    return std::unexpected(Error{ErrorCode::Unavailable, "No native camera pairing adapter is installed for this platform"});
}
} // namespace iiCameraIntergration
