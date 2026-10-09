#include <iiCameraIntergration/Devices.h>
#include <iiCameraIntergration/NativeCamera.h>

#include <algorithm>
#include <exception>
#include <mutex>
#include <set>

namespace iiCameraIntergration {
namespace {
Error invalid(std::string message) { return {ErrorCode::InvalidArgument, std::move(message)}; }
Error backend(std::string message) { return {ErrorCode::BackendFailure, std::move(message)}; }
Error closed() { return {ErrorCode::Cancelled, "The camera pairing has been released"}; }
bool methodValid(PairingMethod method) {
    return method == PairingMethod::Session || method == PairingMethod::DeviceBond;
}
bool idValid(const std::string& id) {
    if (id.empty()) return false;
    for (std::size_t i = 0; i < id.size();) {
        const auto first = static_cast<unsigned char>(id[i++]);
        if (!first) return false;
        if (first < 0x80) continue;
        unsigned count{}, code{}, minimum{};
        if (first >= 0xc2 && first <= 0xdf) { count=1; code=first&0x1f; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count=2; code=first&0xf; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count=3; code=first&7; minimum=0x10000; }
        else return false;
        if (count > id.size()-i) return false;
        while (count--) {
            const auto next=static_cast<unsigned char>(id[i++]);
            if ((next&0xc0)!=0x80) return false;
            code=(code<<6)|(next&0x3f);
        }
        if (code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return false;
    }
    return true;
}
template<class Function> auto invokeProvider(Function&& function) -> decltype(function()) {
    try { return function(); }
    catch (const std::exception& error) { return std::unexpected(backend(error.what())); }
    catch (...) { return std::unexpected(backend("Camera device provider threw an unknown exception")); }
}
}

struct CameraPairing::State {
    State(PairingInfo value, PairingCallbacks functions)
        : info(std::move(value)), callbacks(std::move(functions)) {}
    PairingInfo info;
    PairingCallbacks callbacks;
    std::mutex mutex;
    PairingState observed{PairingState::Ready};
    bool released{};
    // Ports belong to the pairing. Escaped wrappers capture only a weak gate
    // and an index, so they cannot keep native/provider resources alive.
    std::vector<ControlPort> ports;

    Result<PairingState> statusLocked() {
        if (released) return PairingState::Unpaired;
        if (observed != PairingState::Ready) return observed;
        auto result=invokeProvider([&] { return callbacks.status(); });
        if (!result) return std::unexpected(result.error());
        if (*result != PairingState::Ready && *result != PairingState::Disconnected && *result != PairingState::Unpaired)
            return std::unexpected(backend("The camera provider returned an invalid pairing state"));
        observed=*result;
        return observed;
    }
    Result<void> readyLocked() {
        auto current=statusLocked();
        if (!current) return std::unexpected(current.error());
        if (*current == PairingState::Unpaired) return std::unexpected(closed());
        if (*current == PairingState::Disconnected)
            return std::unexpected(Error{ErrorCode::NotFound, "The paired camera is disconnected; create a new pairing"});
        return {};
    }
    Result<void> unpairLocked() {
        if (released) return {};
        auto result=invokeProvider([&] { return callbacks.unpair(); });
        if (!result) return result;
        revokeLocked();
        return {};
    }
    void revokeLocked() {
        released=true;
        observed=PairingState::Unpaired;
        ports.clear();
        callbacks={};
    }
    void abandon() noexcept {
        try {
            std::lock_guard lock(mutex);
            if (!released) {
                (void)unpairLocked();
                // Destruction/move replacement revokes the gate even when the
                // provider's final resource-release attempt fails.
                revokeLocked();
            }
        } catch (...) {}
    }
};

Result<CameraPairing> CameraPairing::create(PairingInfo info, PairingCallbacks callbacks) {
    return invokeProvider([&]() -> Result<CameraPairing> {
        if (!idValid(info.device.id) || !methodValid(info.method))
            return std::unexpected(invalid("Pairing requires a nonempty UTF-8 device ID and a valid method"));
        if (auto valid=validateCameraInfo(info.device); !valid) return std::unexpected(valid.error());
        if (!info.device.connected) return std::unexpected(Error{ErrorCode::NotFound, "The camera is not connected"});
        if (!callbacks.status || !callbacks.controls || !callbacks.unpair)
            return std::unexpected(invalid("Pairing requires status, controls and unpair callbacks"));
        return CameraPairing(std::make_shared<State>(std::move(info), std::move(callbacks)));
    });
}
CameraPairing::CameraPairing(std::shared_ptr<State> state) : state_(std::move(state)) {}
CameraPairing::CameraPairing(CameraPairing&&) noexcept = default;
CameraPairing& CameraPairing::operator=(CameraPairing&& other) noexcept {
    if (this != &other) {
        if (state_) state_->abandon();
        state_=std::move(other.state_);
    }
    return *this;
}
CameraPairing::~CameraPairing() { if (state_) state_->abandon(); }
Result<PairingInfo> CameraPairing::info() const {
    if (!state_) return std::unexpected(invalid("The camera pairing was moved from"));
    return invokeProvider([&]() -> Result<PairingInfo> {
        std::lock_guard lock(state_->mutex);
        return state_->info;
    });
}
Result<PairingState> CameraPairing::status() const {
    if (!state_) return std::unexpected(invalid("The camera pairing was moved from"));
    return invokeProvider([&]() -> Result<PairingState> {
        std::lock_guard lock(state_->mutex);
        return state_->statusLocked();
    });
}
Result<void> CameraPairing::unpair() {
    if (!state_) return std::unexpected(invalid("The camera pairing was moved from"));
    return invokeProvider([&]() -> Result<void> {
        std::lock_guard lock(state_->mutex);
        return state_->unpairLocked();
    });
}
Result<ControlPort> CameraPairing::controls() const {
    if (!state_) return std::unexpected(invalid("The camera pairing was moved from"));
    return invokeProvider([&]() -> Result<ControlPort> {
        std::lock_guard lock(state_->mutex);
        if (auto ready=state_->readyLocked(); !ready) return std::unexpected(ready.error());
        auto provider=invokeProvider([&] { return state_->callbacks.controls(); });
        if (!provider) return std::unexpected(provider.error());
        auto capabilities=provider->capabilities();
        const auto index=state_->ports.size();
        state_->ports.push_back(std::move(*provider));
        std::weak_ptr<State> weak=state_;
        ControlCallbacks guarded;
        guarded.query=[weak,index](ControlId id) -> Result<ControlValue> {
            auto state=weak.lock();
            if (!state) return std::unexpected(closed());
            std::lock_guard lock(state->mutex);
            if (auto ready=state->readyLocked(); !ready) return std::unexpected(ready.error());
            return state->ports[index].query(id);
        };
        guarded.apply=[weak,index](const ControlCommand& command) -> Result<ControlValue> {
            auto state=weak.lock();
            if (!state) return std::unexpected(closed());
            std::lock_guard lock(state->mutex);
            if (auto ready=state->readyLocked(); !ready) return std::unexpected(ready.error());
            return state->ports[index].apply(command);
        };
        return ControlPort(std::move(capabilities), std::move(guarded));
    });
}

CameraDeviceSystem::CameraDeviceSystem(DeviceCallbacks callbacks) : callbacks_(std::move(callbacks)) {}
Result<std::vector<CameraInfo>> CameraDeviceSystem::discover() const {
    if (!callbacks_.discover) return std::unexpected(Error{ErrorCode::Unavailable, "No camera discovery provider"});
    return invokeProvider([&]() -> Result<std::vector<CameraInfo>> {
        auto devices=callbacks_.discover();
        if (!devices) return std::unexpected(devices.error());
        std::set<std::string> identifiers;
        for (const auto& device : *devices) {
            if (!idValid(device.id) || !identifiers.insert(device.id).second)
                return std::unexpected(backend("Camera discovery returned duplicate or invalid UTF-8 identifiers"));
            if (auto valid=validateCameraInfo(device); !valid)
                return std::unexpected(backend("Invalid discovered camera metadata: "+valid.error().message));
        }
        return devices;
    });
}
Result<CameraPairing> CameraDeviceSystem::pair(const std::string& id, const PairingOptions& options) const {
    if (!idValid(id) || !methodValid(options.method))
        return std::unexpected(invalid("Pairing requires a nonempty UTF-8 device ID and a valid method"));
    if (!callbacks_.pair) return std::unexpected(Error{ErrorCode::Unavailable, "No camera pairing provider"});
    return invokeProvider([&]() -> Result<CameraPairing> {
        auto devices=discover();
        if (!devices) return std::unexpected(devices.error());
        if (std::none_of(devices->begin(), devices->end(), [&](const auto& device) { return device.id==id && device.connected; }))
            return std::unexpected(Error{ErrorCode::NotFound, "The requested camera is not currently connected"});
        auto paired=callbacks_.pair(id, options);
        if (!paired) return std::unexpected(paired.error());
        auto info=paired->info();
        if (!info || info->device.id!=id || info->method!=options.method)
            return std::unexpected(backend("The camera provider paired a different device or method"));
        auto ready=paired->status();
        if (!ready) return std::unexpected(ready.error());
        if (*ready != PairingState::Ready)
            return std::unexpected(backend("The camera provider did not return a ready pairing"));
        return paired;
    });
}
CameraDeviceSystem NativeCameraSystem::devices() const {
    return CameraDeviceSystem({
        [] { return NativeCameraSystem{}.cameras(); },
        [](const std::string& id, const PairingOptions& options) { return NativeCameraSystem{}.pair(id, options); }
    });
}
} // namespace iiCameraIntergration
