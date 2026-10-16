#include <iiCameraIntergration/NativeCamera.h>
#include <camera/NdkCameraManager.h>
#include <map>
#include <iostream>
#include <stdexcept>

using namespace iiCameraIntergration;
struct ACameraManager {};
struct ACameraMetadata {};
struct ACameraDevice {ACameraDevice_StateCallbacks callbacks;};
namespace {
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
int managers{},metadataHandles{},opened{},closed{};camera_status_t openError=ACAMERA_OK;
ACameraDevice* active{};
const std::map<std::uint32_t,ACameraMetadata_const_entry>& entries() {
    static const std::uint8_t facing[]={ACAMERA_LENS_FACING_BACK};
    static const float physical[]={6.4F,4.8F},focal[]={4.0F,8.0F},aperture[]={1.8F,2.8F};
    static const std::int32_t pixels[]={4000,3000},streams[]={35,1920,1080,0,35,640,480,1};
    static const std::int64_t durations[]={35,1920,1080,33333333},vendor[]={INT64_C(9007199254740993)};
    static const std::map<std::uint32_t,ACameraMetadata_const_entry> values={
        {ACAMERA_LENS_FACING,{.tag=ACAMERA_LENS_FACING,.type=ACAMERA_TYPE_BYTE,.count=1,.data={.u8=facing}}},
        {ACAMERA_SENSOR_INFO_PHYSICAL_SIZE,{.tag=ACAMERA_SENSOR_INFO_PHYSICAL_SIZE,.type=ACAMERA_TYPE_FLOAT,.count=2,.data={.f=physical}}},
        {ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE,{.tag=ACAMERA_SENSOR_INFO_PIXEL_ARRAY_SIZE,.type=ACAMERA_TYPE_INT32,.count=2,.data={.i32=pixels}}},
        {ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,{.tag=ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,.type=ACAMERA_TYPE_FLOAT,.count=2,.data={.f=focal}}},
        {ACAMERA_LENS_INFO_AVAILABLE_APERTURES,{.tag=ACAMERA_LENS_INFO_AVAILABLE_APERTURES,.type=ACAMERA_TYPE_FLOAT,.count=2,.data={.f=aperture}}},
        {ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,{.tag=ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,.type=ACAMERA_TYPE_INT32,.count=8,.data={.i32=streams}}},
        {ACAMERA_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,{.tag=ACAMERA_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,.type=ACAMERA_TYPE_INT64,.count=4,.data={.i64=durations}}},
        {0x80000001U,{.tag=0x80000001U,.type=ACAMERA_TYPE_INT64,.count=1,.data={.i64=vendor}}}
    };
    return values;
}
}
extern "C" {
ACameraManager* ACameraManager_create(){++managers;return new ACameraManager;}
void ACameraManager_delete(ACameraManager* value){--managers;delete value;}
camera_status_t ACameraManager_getCameraIdList(ACameraManager*,ACameraIdList** output) {
    static const char* values[]={"0"};*output=new ACameraIdList{1,values};return ACAMERA_OK;
}
void ACameraManager_deleteCameraIdList(ACameraIdList* value){delete value;}
camera_status_t ACameraManager_getCameraCharacteristics(ACameraManager*,const char*,ACameraMetadata** output){++metadataHandles;*output=new ACameraMetadata;return ACAMERA_OK;}
void ACameraMetadata_free(ACameraMetadata* value){--metadataHandles;delete value;}
camera_status_t ACameraMetadata_getConstEntry(const ACameraMetadata*,std::uint32_t tag,ACameraMetadata_const_entry* output) {
    const auto it=entries().find(tag);if(it==entries().end())return ACAMERA_ERROR_METADATA_NOT_FOUND;*output=it->second;return ACAMERA_OK;
}
camera_status_t ACameraMetadata_getAllTags(const ACameraMetadata*,int32_t* count,const std::uint32_t** output) {
    static const auto tags=[] {std::vector<std::uint32_t> values;for(const auto& [id,e]:entries()) {values.push_back(id);(void)e;}return values;}();
    *count=static_cast<int32_t>(tags.size());*output=tags.data();return ACAMERA_OK;
}
camera_status_t ACameraManager_openCamera(ACameraManager*,const char*,ACameraDevice_StateCallbacks* callbacks,ACameraDevice** output) {
    if(openError!=ACAMERA_OK)return openError;++opened;active=new ACameraDevice{*callbacks};*output=active;return ACAMERA_OK;
}
camera_status_t ACameraDevice_close(ACameraDevice* value){++closed;active=nullptr;delete value;return ACAMERA_OK;}
}
int main() {
    try {
        NativeCameraSystem system;
        auto report=system.features("0");CHECK(report && validateCameraInfo(report->camera));
        CHECK(report->camera.position==Position::Back && report->camera.sensor->resolution->width==4000);
        CHECK(report->camera.lenses[0].focalLengthMm->discreteValues.size()==2);
        CHECK(!report->camera.lenses[0].currentFocalLengthMm && !report->camera.activeFormatId);
        CHECK(report->camera.formats.size()==1 && report->camera.formats[0].frameRates.empty());
        CHECK(report->nativeFeatures.size()==entries().size() && opened==0);
        CHECK(std::get<std::vector<std::int64_t>>(**report->nativeFeatures.back().value)[0]==INT64_C(9007199254740993));
        auto metadata=system.features("0",FeatureQueryOptions{false});CHECK(metadata && metadata->nativeFeatures.back().value && opened==0);
        auto missing=system.features("missing");CHECK(!missing && missing.error().code==ErrorCode::NotFound);
        auto invalid=system.features(std::string("\xff",1));CHECK(!invalid && invalid.error().code==ErrorCode::InvalidArgument);
        auto cameras=system.cameras();CHECK(cameras && cameras->size()==1 && managers==0 && metadataHandles==0);
        openError=ACAMERA_ERROR_PERMISSION_DENIED;auto denied=system.pair("0");CHECK(!denied && denied.error().code==ErrorCode::PermissionDenied);
        openError=ACAMERA_OK;auto paired=system.pair("0");CHECK(paired && opened==1 && paired->features());
        active->callbacks.onDisconnected(active->callbacks.context,active);
        const auto status=paired->status();CHECK(status && *status==PairingState::Disconnected);
        CHECK(!paired->features() && paired->features().error().code==ErrorCode::NotFound);
        CHECK(paired->unpair() && closed==1);
        auto released=paired->features();CHECK(!released && released.error().code==ErrorCode::Cancelled);
        paired=std::unexpected(Error{ErrorCode::Cancelled,"fixture complete"});CHECK(managers==0 && metadataHandles==0);
        std::cout<<"Camera2 artificial metadata, vendor tags, permission and lifecycle contracts passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
