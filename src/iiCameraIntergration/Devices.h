#pragma once

#include <iiCameraIntergration/Camera.h>
#include <memory>

namespace iiCameraIntergration {

// Session prepares camera input resources without starting capture. DeviceBond
// requires a provider that actually implements the device's authentication.
enum class PairingMethod { Session, DeviceBond };
enum class PairingState { Ready, Disconnected, Unpaired };
struct PairingOptions {
    PairingMethod method{PairingMethod::Session};
    Metadata parameters; // Provider-specific parameters; never copied into info.
};
struct PairingInfo {
    CameraInfo device;
    PairingMethod method{PairingMethod::Session};
    Metadata additional;
};
struct PairingCallbacks {
    std::function<Result<PairingState>()> status;
    std::function<Result<ControlPort>()> controls;
    std::function<Result<void>()> unpair;
};

// Single owner; returned control ports retain only a weak, revocable session gate.
// Operations on a live instance/control port are serialized. Moving/destroying
// the same instance concurrently is not allowed. Callbacks must not reenter it.
class IICAMERAINTERGRATION_EXPORT CameraPairing final {
public:
    [[nodiscard]] static Result<CameraPairing> create(PairingInfo, PairingCallbacks);
    CameraPairing(CameraPairing&&) noexcept;
    CameraPairing& operator=(CameraPairing&&) noexcept;
    CameraPairing(const CameraPairing&) = delete;
    CameraPairing& operator=(const CameraPairing&) = delete;
    ~CameraPairing();
    [[nodiscard]] Result<PairingInfo> info() const;
    [[nodiscard]] Result<PairingState> status() const;
    [[nodiscard]] Result<ControlPort> controls() const;
    // Successful repeated unpair is idempotent. Failure keeps ownership so the
    // caller can retry; destruction makes one final best-effort release attempt.
    [[nodiscard]] Result<void> unpair();
private:
    struct State;
    explicit CameraPairing(std::shared_ptr<State>);
    std::shared_ptr<State> state_;
};

struct DeviceCallbacks {
    std::function<Result<std::vector<CameraInfo>>()> discover;
    std::function<Result<CameraPairing>(const std::string&, const PairingOptions&)> pair;
};

// A concrete callback port, like ControlPort. Discovery does not establish a
// connection. pair re-discovers the ID and validates the provider's ready handle.
class IICAMERAINTERGRATION_EXPORT CameraDeviceSystem final {
public:
    explicit CameraDeviceSystem(DeviceCallbacks);
    [[nodiscard]] Result<std::vector<CameraInfo>> discover() const;
    [[nodiscard]] Result<CameraPairing> pair(const std::string& deviceId,
        const PairingOptions& options = {}) const;
private:
    DeviceCallbacks callbacks_;
};
} // namespace iiCameraIntergration
