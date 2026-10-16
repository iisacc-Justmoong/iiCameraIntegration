#include "NativeCamera.h"
#include "detail/Identifiers.h"
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadataTags.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace iiCameraIntergration {
namespace {
Error error(camera_status_t code,const char* action) {
    return {code==ACAMERA_ERROR_PERMISSION_DENIED?ErrorCode::PermissionDenied:
        code==ACAMERA_ERROR_CAMERA_IN_USE || code==ACAMERA_ERROR_MAX_CAMERA_IN_USE?ErrorCode::Busy:
        code==ACAMERA_ERROR_CAMERA_DISCONNECTED?ErrorCode::NotFound:ErrorCode::BackendFailure,
        std::string(action)+" (Camera2 status "+std::to_string(code)+")",code};
}
using Manager=std::unique_ptr<ACameraManager,decltype(&ACameraManager_delete)>;
using MetadataHandle=std::unique_ptr<ACameraMetadata,decltype(&ACameraMetadata_free)>;
Result<MetadataHandle> characteristics(ACameraManager* manager,const std::string& id) {
    if(!detail::validDeviceId(id)) return std::unexpected(Error{ErrorCode::InvalidArgument,"A nonempty UTF-8 camera ID is required"});
    ACameraIdList* raw{};auto status=ACameraManager_getCameraIdList(manager,&raw);
    if(status!=ACAMERA_OK) return std::unexpected(error(status,"Enumerating Camera2 IDs"));
    std::unique_ptr<ACameraIdList,decltype(&ACameraManager_deleteCameraIdList)> ids(raw,ACameraManager_deleteCameraIdList);
    bool found=false;for(int i=0;i<ids->numCameras;++i) if(id==ids->cameraIds[i]) found=true;
    if(!found) return std::unexpected(Error{ErrorCode::NotFound,"The requested Camera2 camera is not connected"});
    ACameraMetadata* data{};status=ACameraManager_getCameraCharacteristics(manager,id.c_str(),&data);
    if(status!=ACAMERA_OK) return std::unexpected(error(status,"Reading Camera2 characteristics"));
    return MetadataHandle(data,ACameraMetadata_free);
}
std::optional<ACameraMetadata_const_entry> entry(const ACameraMetadata* data,std::uint32_t tag) {
    ACameraMetadata_const_entry value{};
    if(ACameraMetadata_getConstEntry(data,tag,&value)!=ACAMERA_OK) return {};
    return value;
}
Result<FeatureValue> value(const ACameraMetadata_const_entry& e) {
    switch(e.type) {
    case ACAMERA_TYPE_BYTE:case ACAMERA_TYPE_INT32:case ACAMERA_TYPE_INT64: {
        std::vector<std::int64_t> items;items.reserve(e.count);
        for(std::uint32_t i=0;i<e.count;++i) items.push_back(e.type==ACAMERA_TYPE_BYTE?e.data.u8[i]:e.type==ACAMERA_TYPE_INT32?e.data.i32[i]:e.data.i64[i]);
        return FeatureValue{std::move(items)};
    }
    case ACAMERA_TYPE_FLOAT:case ACAMERA_TYPE_DOUBLE: {
        std::vector<double> items;items.reserve(e.count);
        for(std::uint32_t i=0;i<e.count;++i) items.push_back(e.type==ACAMERA_TYPE_FLOAT?e.data.f[i]:e.data.d[i]);
        return FeatureValue{std::move(items)};
    }
    case ACAMERA_TYPE_RATIONAL: {
        // Interleaved numerator/denominator pairs preserve exact rationals.
        std::vector<std::int64_t> items;items.reserve(e.count*2);
        for(std::uint32_t i=0;i<e.count;++i) {items.push_back(e.data.r[i].numerator);items.push_back(e.data.r[i].denominator);}
        return FeatureValue{std::move(items)};
    }
    default:return std::unexpected(Error{ErrorCode::Unsupported,"Unknown Camera2 metadata type"});
    }
}
Result<CameraFeatures> describe(const std::string& id,const ACameraMetadata* data,const FeatureQueryOptions& options) {
    CameraFeatures result;auto& camera=result.camera;camera.id=id;camera.name="Camera2 camera "+id;camera.backend="Camera2";
    camera.additional["Camera2.metadataScope"]="static characteristics; current settings require capture-result metadata";
    if(auto facing=entry(data,ACAMERA_LENS_FACING);facing && facing->count) {
        camera.position=facing->data.u8[0]==ACAMERA_LENS_FACING_FRONT?Position::Front:
            facing->data.u8[0]==ACAMERA_LENS_FACING_BACK?Position::Back:Position::External;
        if(camera.position==Position::Front || camera.position==Position::Back) camera.transport=Transport::BuiltIn;
    }
    SensorInfo sensor;
    if(auto physical=entry(data,ACAMERA_SENSOR_INFO_PHYSICAL_SIZE);physical && physical->count>=2) {
        if(physical->data.f[0]>0 && std::isfinite(physical->data.f[0])) sensor.widthMm=physical->data.f[0];
        if(physical->data.f[1]>0 && std::isfinite(physical->data.f[1])) sensor.heightMm=physical->data.f[1];
    }
    if(auto pixels=entry(data,ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE);pixels && pixels->count>=2 && pixels->data.i32[0]>0 && pixels->data.i32[1]>0)
        sensor.resolution=Size{static_cast<std::uint32_t>(pixels->data.i32[0]),static_cast<std::uint32_t>(pixels->data.i32[1])};
    if(sensor.widthMm || sensor.heightMm || sensor.resolution) camera.sensor=sensor;
    LensInfo lens;lens.id="Camera2.reportedLens";
    const auto optical=[&](std::uint32_t tag) -> std::optional<NumericRange> {
        auto e=entry(data,tag);if(!e || !e->count) return {};
        std::vector<double> values;
        for(std::uint32_t i=0;i<e->count;++i) if(e->data.f[i]>0 && std::isfinite(e->data.f[i])) values.push_back(e->data.f[i]);
        if(values.empty()) return {};const auto bounds=std::minmax_element(values.begin(),values.end());
        return NumericRange{*bounds.first,*bounds.second,0,std::move(values)};
    };
    lens.focalLengthMm=optical(ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
    lens.apertureFNumber=optical(ACAMERA_LENS_INFO_AVAILABLE_APERTURES);
    if(lens.focalLengthMm || lens.apertureFNumber) camera.lenses.push_back(lens);
    if(auto streams=entry(data,ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);streams) {
        auto durations=entry(data,ACAMERA_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
        for(std::uint32_t i=0;i+3<streams->count;i+=4) {
            const auto* row=&streams->data.i32[i];if(row[3]!=0 || row[1]<=0 || row[2]<=0) continue;
            CameraFormat format;format.id="Camera2:"+std::to_string(i/4);
            format.resolution={static_cast<std::uint32_t>(row[1]),static_cast<std::uint32_t>(row[2])};
            format.additional["Camera2.imageFormat"]=std::to_string(row[0]);
            if(durations) for(std::uint32_t j=0;j+3<durations->count;j+=4) {
                const auto* duration=&durations->data.i64[j];
                if(duration[0]==row[0] && duration[1]==row[1] && duration[2]==row[2] && duration[3]>0)
                    format.additional["Camera2.minimumFrameDurationNanoseconds"]=std::to_string(duration[3]);
            }
            // FPS depends on request and stream combinations, not just this size.
            camera.formats.push_back(std::move(format));
        }
    }
    int32_t count{};const std::uint32_t* tags{};auto status=ACameraMetadata_getAllTags(data,&count,&tags);
    if(status!=ACAMERA_OK) return std::unexpected(error(status,"Enumerating Camera2 metadata tags"));
    for(int32_t i=0;i<count;++i) {
        NativeFeature feature;feature.id="Camera2:"+std::to_string(tags[i]);feature.name=feature.id;feature.type=FeatureType::Compound;feature.readable=true;
        feature.additional["Camera2.scope"]="static characteristic";
        if(auto e=entry(data,tags[i]);e) {
            feature.additional["Camera2.type"]=std::to_string(e->type);feature.additional["Camera2.count"]=std::to_string(e->count);
            feature.value=value(*e);
        } else feature.value=std::unexpected(Error{ErrorCode::BackendFailure,"Camera2 tag could not be read"});
        // Static characteristic values remain available in metadata-only mode.
        result.nativeFeatures.push_back(std::move(feature));
    }
    camera.additional["Camera2.currentValuesRequested"]=options.includeCurrentValues?"true":"false";
    camera.additional["Camera2.controlScope"]="capture requests and results belong to the host capture session";
    return result;
}
struct Connection {
    Manager manager{ACameraManager_create(),ACameraManager_delete};ACameraDevice* device{};
    std::atomic<PairingState> state{PairingState::Ready};std::atomic<int> failure{};
    ~Connection(){if(device) ACameraDevice_close(device);}
};
}
bool NativeCameraSystem::available() const noexcept {return true;}
CameraPermission NativeCameraSystem::permissionStatus() const noexcept {return CameraPermission::NotDetermined;}
Result<CameraFeatures> NativeCameraSystem::features(const std::string& id,const FeatureQueryOptions& options) const {
    Manager manager(ACameraManager_create(),ACameraManager_delete);
    if(!manager) return std::unexpected(Error{ErrorCode::Unavailable,"Camera2 manager is unavailable"});
    auto metadata=characteristics(manager.get(),id);if(!metadata) return std::unexpected(metadata.error());
    return describe(id,metadata->get(),options);
}
Result<std::vector<CameraInfo>> NativeCameraSystem::cameras() const {
    Manager manager(ACameraManager_create(),ACameraManager_delete);
    if(!manager) return std::unexpected(Error{ErrorCode::Unavailable,"Camera2 manager is unavailable"});
    ACameraIdList* raw{};const auto status=ACameraManager_getCameraIdList(manager.get(),&raw);
    if(status!=ACAMERA_OK) return std::unexpected(error(status,"Enumerating cameras"));
    std::unique_ptr<ACameraIdList,decltype(&ACameraManager_deleteCameraIdList)> ids(raw,ACameraManager_deleteCameraIdList);
    std::vector<CameraInfo> result;
    for(int i=0;i<ids->numCameras;++i) {
        const std::string id=ids->cameraIds[i];auto report=features(id,FeatureQueryOptions{false});
        if(report) result.push_back(std::move(report->camera));
        else {
            CameraInfo camera;camera.id=id;camera.name="Camera2 camera "+id;camera.backend="Camera2";
            camera.additional["Camera2.discoveryError"]=report.error().message;result.push_back(std::move(camera));
        }
    }
    return result;
}
Result<ControlPort> NativeCameraSystem::controls(const std::string& id) const {
    auto report=features(id,FeatureQueryOptions{false});if(!report) return std::unexpected(report.error());
    // Static characteristics are exposed through features; no observed control
    // value is fabricated without a host-owned capture request/result session.
    return ControlPort({},{});
}
Result<CameraPairing> NativeCameraSystem::pair(const std::string& id,const PairingOptions& options) const {
    if(!detail::validDeviceId(id)) return std::unexpected(Error{ErrorCode::InvalidArgument,"A nonempty UTF-8 camera ID is required"});
    if(options.method==PairingMethod::DeviceBond || !options.parameters.empty())
        return std::unexpected(Error{ErrorCode::Unsupported,"Camera2 sessions do not perform device bonding or accept pairing parameters"});
    if(options.method!=PairingMethod::Session) return std::unexpected(Error{ErrorCode::InvalidArgument,"Unknown pairing method"});
    auto info=features(id,FeatureQueryOptions{false});if(!info) return std::unexpected(info.error());
    auto connection=std::make_shared<Connection>();
    if(!connection->manager) return std::unexpected(Error{ErrorCode::Unavailable,"Camera2 manager is unavailable"});
    ACameraDevice_StateCallbacks callbacks{};callbacks.context=connection.get();
    callbacks.onDisconnected=[](void* context,ACameraDevice*) {static_cast<Connection*>(context)->state=PairingState::Disconnected;};
    callbacks.onError=[](void* context,ACameraDevice*,int code) {auto* state=static_cast<Connection*>(context);state->failure=code;state->state=PairingState::Disconnected;};
    const auto status=ACameraManager_openCamera(connection->manager.get(),id.c_str(),&callbacks,&connection->device);
    if(status!=ACAMERA_OK) return std::unexpected(error(status,"Opening the Camera2 device"));
    return CameraPairing::create({info->camera,options.method,{{"Camera2.connection","open device; capture session not created"}}},{
        [connection]() -> Result<PairingState> {return connection->state.load();},
        []() -> Result<ControlPort> {return ControlPort({},{});},
        [connection]() -> Result<void> {
            if(connection->device) {
                const auto code=ACameraDevice_close(connection->device);
                // NDK close frees the handle synchronously even on failure.
                connection->device=nullptr;if(code!=ACAMERA_OK) return std::unexpected(error(code,"Closing the Camera2 device"));
            }
            connection->state=PairingState::Unpaired;return {};
        },
        [id](const FeatureQueryOptions& query) {return NativeCameraSystem{}.features(id,query);}});
}
} // namespace iiCameraIntergration
