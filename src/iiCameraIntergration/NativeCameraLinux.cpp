#include "NativeCamera.h"
#include "detail/V4L2.h"
#include "detail/Identifiers.h"
#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <mutex>

namespace iiCameraIntergration {
namespace {
Error osError(const char* action) {
    const int value=errno;
    return {value==EACCES||value==EPERM?ErrorCode::PermissionDenied:value==ENOENT||value==ENODEV?ErrorCode::NotFound:
        value==EBUSY?ErrorCode::Busy:ErrorCode::Io,std::string(action)+": "+std::strerror(value),value};
}
struct Device {
    explicit Device(int value,bool write):fd(value),writable(write){}
    ~Device(){if(fd>=0) ::close(fd);}
    int fd;bool writable;std::mutex mutex;
    detail::V4L2Ioctl io() const {
        return [this](unsigned long request,void* data) {
            int value;do {value=::ioctl(fd,request,data);} while(value<0 && errno==EINTR);return value;
        };
    }
};
bool validNode(const std::string& id) {
    constexpr std::string_view prefix="/dev/video";
    return id.starts_with(prefix) && id.size()>prefix.size() &&
        std::all_of(id.begin()+prefix.size(),id.end(),[](unsigned char c){return c>='0'&&c<='9';});
}
Result<std::shared_ptr<Device>> openDevice(const std::string& id,bool write) {
    if(!detail::validDeviceId(id))
        return std::unexpected(Error{ErrorCode::InvalidArgument,"A valid camera ID is required"});
    if(!validNode(id)) return std::unexpected(Error{ErrorCode::NotFound,"The requested V4L2 capture node is not connected"});
    int fd=::open(id.c_str(),(write?O_RDWR:O_RDONLY)|O_NONBLOCK|O_CLOEXEC);
    bool writable=write;
    if(fd<0 && write && (errno==EACCES || errno==EPERM)) {fd=::open(id.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);writable=false;}
    if(fd<0) return std::unexpected(osError("Opening the V4L2 camera"));
    auto device=std::make_shared<Device>(fd,writable);
    v4l2_capability cap{};
    if(device->io()(VIDIOC_QUERYCAP,&cap)<0) return std::unexpected(osError("Querying the V4L2 camera"));
    const auto bits=cap.capabilities&V4L2_CAP_DEVICE_CAPS?cap.device_caps:cap.capabilities;
    if(!(bits&(V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_VIDEO_CAPTURE_MPLANE)))
        return std::unexpected(Error{ErrorCode::Unsupported,"The node is not a video capture camera"});
    return device;
}
ControlPort port(const std::shared_ptr<Device>& device,std::vector<ControlCapability> capabilities) {
    return ControlPort(std::move(capabilities),{
        [device](ControlId id) -> Result<ControlValue> {std::lock_guard lock(device->mutex);return detail::readV4L2(device->io(),id);},
        [device](const ControlCommand& command) -> Result<ControlValue> {
            std::lock_guard lock(device->mutex);
            if(!device->writable) return std::unexpected(Error{ErrorCode::PermissionDenied,"The camera was opened read-only"});
            return detail::applyV4L2(device->io(),command);
        }});
}
}
bool NativeCameraSystem::available() const noexcept {return true;}
CameraPermission NativeCameraSystem::permissionStatus() const noexcept {return CameraPermission::NotDetermined;}
Result<std::vector<CameraInfo>> NativeCameraSystem::cameras() const {
    std::vector<CameraInfo> result;std::error_code error;
    std::filesystem::directory_iterator entries("/dev",error);
    if(error) return std::unexpected(Error{ErrorCode::Io,"Cannot enumerate /dev: "+error.message(),error.value()});
    for(const auto& entry:entries) {
        const auto id=entry.path().string();if(!validNode(id)) continue;
        auto device=openDevice(id,false);
        if(!device) {
            if(device.error().code==ErrorCode::PermissionDenied || device.error().code==ErrorCode::Busy) {
                CameraInfo info;info.id=id;info.name=entry.path().filename().string();info.backend="V4L2";
                info.additional["V4L2.discoveryError"]=device.error().message;result.push_back(std::move(info));
            }
            continue;
        }
        auto report=detail::describeV4L2(id,(*device)->io(),true,false);
        if(report) result.push_back(std::move(report->camera));
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
Result<ControlPort> NativeCameraSystem::controls(const std::string& id) const {
    auto device=openDevice(id,true);if(!device) return std::unexpected(device.error());
    auto report=detail::describeV4L2(id,(*device)->io(),(*device)->writable,false);
    if(!report) return std::unexpected(report.error());
    return port(*device,std::move(report->camera.controls));
}
Result<CameraFeatures> NativeCameraSystem::features(const std::string& id,const FeatureQueryOptions& options) const {
    auto device=openDevice(id,false);if(!device) return std::unexpected(device.error());
    // No configuration, streaming ioctl, or write access is needed to inspect.
    return detail::describeV4L2(id,(*device)->io(),true,options.includeCurrentValues);
}
Result<CameraPairing> NativeCameraSystem::pair(const std::string& id,const PairingOptions& options) const {
    if(!detail::validDeviceId(id))
        return std::unexpected(Error{ErrorCode::InvalidArgument,"A valid camera ID is required"});
    if(options.method==PairingMethod::DeviceBond || !options.parameters.empty())
        return std::unexpected(Error{ErrorCode::Unsupported,"V4L2 sessions do not perform device bonding or accept pairing parameters"});
    if(options.method!=PairingMethod::Session) return std::unexpected(Error{ErrorCode::InvalidArgument,"Unknown pairing method"});
    auto device=openDevice(id,true);if(!device) return std::unexpected(device.error());
    auto report=detail::describeV4L2(id,(*device)->io(),(*device)->writable,false);
    if(!report) return std::unexpected(report.error());
    auto state=std::make_shared<std::shared_ptr<Device>>(*device);
    auto info=report->camera;
    return CameraPairing::create({info,options.method,{{"V4L2.connection","open camera descriptor; streaming not started"}}},{
        [state]() -> Result<PairingState> {
            if(!*state) return PairingState::Unpaired;
            v4l2_capability cap{};
            if((*state)->io()(VIDIOC_QUERYCAP,&cap)<0) {
                if(errno==ENODEV || errno==ENXIO || errno==EBADF) return PairingState::Disconnected;
                return std::unexpected(osError("Checking the V4L2 connection"));
            }
            return PairingState::Ready;
        },
        [state,id]() -> Result<ControlPort> {
            if(!*state) return std::unexpected(Error{ErrorCode::Cancelled,"V4L2 session released"});
            auto current=detail::describeV4L2(id,(*state)->io(),(*state)->writable,false);
            if(!current) return std::unexpected(current.error());
            return port(*state,std::move(current->camera.controls));
        },
        [state]() -> Result<void> {state->reset();return {};},
        [state,id](const FeatureQueryOptions& query) -> Result<CameraFeatures> {
            if(!*state) return std::unexpected(Error{ErrorCode::Cancelled,"V4L2 session released"});
            std::lock_guard lock((*state)->mutex);
            return detail::describeV4L2(id,(*state)->io(),(*state)->writable,query.includeCurrentValues);
        }});
}
} // namespace iiCameraIntergration
