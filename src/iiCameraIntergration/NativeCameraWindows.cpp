#include "NativeCamera.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <dshow.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include <thread>

namespace iiCameraIntergration {
namespace {
using Microsoft::WRL::ComPtr;
Error error(HRESULT hr,const char* action) {
    const auto code=hr==E_ACCESSDENIED?ErrorCode::PermissionDenied:
        hr==MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED || hr==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)?ErrorCode::NotFound:
        hr==HRESULT_FROM_WIN32(ERROR_BUSY)||hr==HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)?ErrorCode::Busy:
        hr==E_NOINTERFACE || hr==E_NOTIMPL?ErrorCode::Unsupported:ErrorCode::BackendFailure;
    return {code,std::string(action)+" (HRESULT "+std::to_string(static_cast<std::uint32_t>(hr))+")",static_cast<int>(hr)};
}
std::string utf8(const wchar_t* value,std::size_t size) {
    if(!size) return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,static_cast<int>(size),nullptr,0,nullptr,nullptr);
    if(!count) return {};
    std::string result(count,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,static_cast<int>(size),result.data(),count,nullptr,nullptr);
    return result;
}
Result<std::wstring> wide(const std::string& id) {
    if(id.empty() || id.find('\0')!=std::string::npos || id.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::unexpected(Error{ErrorCode::InvalidArgument,"A nonempty UTF-8 camera ID is required"});
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id.data(),static_cast<int>(id.size()),nullptr,0);
    if(!count) return std::unexpected(Error{ErrorCode::InvalidArgument,"The camera ID must be valid UTF-8"});
    std::wstring result(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id.data(),static_cast<int>(id.size()),result.data(),count);return result;
}
std::string guid(REFGUID value) {
    wchar_t buffer[40]{};const int size=StringFromGUID2(value,buffer,40);return size?utf8(buffer,size-1):std::string{};
}
// All COM objects and native operations stay on an owned MTA thread. Callers
// may use this API from either STA/UI threads or worker threads.
class Worker final {
public:
    Worker() {
        std::promise<HRESULT> ready;auto result=ready.get_future();
        thread_=std::thread([this,&ready] {
            const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            const auto mf=SUCCEEDED(com)?MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET):com;
            ready.set_value(mf);
            for(;;) {
                std::function<void()> task;
                {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stopping_||!queue_.empty();});
                 if(queue_.empty()&&stopping_) break;task=std::move(queue_.front());queue_.pop_front();}
                task();
            }
            if(SUCCEEDED(mf)) MFShutdown();if(SUCCEEDED(com)) CoUninitialize();
        });
        initial_=result.get();
    }
    ~Worker() {{std::lock_guard lock(mutex_);stopping_=true;}changed_.notify_one();thread_.join();}
    template<class F> auto call(F function) -> decltype(function()) {
        using R=decltype(function());
        if(FAILED(initial_)) return R{std::unexpected(error(initial_,"Initializing Media Foundation"))};
        if(std::this_thread::get_id()==thread_.get_id()) return function();
        auto task=std::make_shared<std::packaged_task<R()>>(std::move(function));auto result=task->get_future();
        {std::lock_guard lock(mutex_);queue_.push_back([task]{(*task)();});}changed_.notify_one();return result.get();
    }
private:
    std::thread thread_;std::mutex mutex_;std::condition_variable changed_;
    std::deque<std::function<void()>> queue_;bool stopping_{};HRESULT initial_{E_FAIL};
};
std::shared_ptr<Worker> worker() {static auto value=std::make_shared<Worker>();return value;}
std::string attributeString(IMFAttributes* attributes,REFGUID key) {
    wchar_t* text{};UINT32 size{};
    if(FAILED(attributes->GetAllocatedString(key,&text,&size))) return {};
    auto result=utf8(text,size);CoTaskMemFree(text);return result;
}
struct ActivationList {
    IMFActivate** values{};UINT32 count{};
    ~ActivationList(){for(UINT32 i=0;i<count;++i) values[i]->Release();CoTaskMemFree(values);}
};
HRESULT enumerate(ActivationList& list) {
    ComPtr<IMFAttributes> attributes;auto hr=MFCreateAttributes(attributes.GetAddressOf(),1);
    if(SUCCEEDED(hr)) hr=attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if(SUCCEEDED(hr)) hr=MFEnumDeviceSources(attributes.Get(),&list.values,&list.count);return hr;
}
CameraInfo identity(IMFActivate* activation) {
    CameraInfo result;result.id=attributeString(activation,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
    result.name=attributeString(activation,MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);if(result.name.empty()) result.name=result.id;
    result.backend="MediaFoundation";result.additional["MediaFoundation.symbolicLink"]=result.id;
    result.additional["MediaFoundation.discoveryScope"]="identity only; feature query activates an unstarted media source";
    return result;
}
struct Source {
    explicit Source(std::shared_ptr<Worker> value):owner(std::move(value)){}
    ~Source() {if(media) (void)owner->call([this]() -> Result<void> {media->Shutdown();camera.Reset();proc.Reset();media.Reset();return {};});}
    std::shared_ptr<Worker> owner;ComPtr<IMFMediaSource> media;
    ComPtr<IAMCameraControl> camera;ComPtr<IAMVideoProcAmp> proc;CameraInfo info;
};
Result<std::shared_ptr<Source>> openSource(const std::string& id) {
    if(auto valid=wide(id);!valid) return std::unexpected(valid.error());
    auto owner=worker();
    return owner->call([owner,id]() -> Result<std::shared_ptr<Source>> {
        ActivationList list;auto hr=enumerate(list);if(FAILED(hr)) return std::unexpected(error(hr,"Enumerating camera devices"));
        for(UINT32 i=0;i<list.count;++i) {
            if(attributeString(list.values[i],MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)!=id) continue;
            auto state=std::make_shared<Source>(owner);state->info=identity(list.values[i]);
            hr=list.values[i]->ActivateObject(IID_PPV_ARGS(state->media.GetAddressOf()));
            if(FAILED(hr)) return std::unexpected(error(hr,"Activating the camera source"));
            // Activation alone does not call Start or read a sample.
            state->media.As(&state->camera);state->media.As(&state->proc);return state;
        }
        return std::unexpected(Error{ErrorCode::NotFound,"The requested camera is not connected"});
    });
}
struct Mapping {ControlId id;bool camera;long property;double scale;const char* unit;bool exposure{};bool mode{};};
constexpr Mapping mappings[] = {
    {ControlId::PanDegrees,true,CameraControl_Pan,1,"degrees"},
    {ControlId::TiltDegrees,true,CameraControl_Tilt,1,"degrees"},
    {ControlId::RollDegrees,true,CameraControl_Roll,1,"degrees"},
    {ControlId::FocalLengthMm,true,CameraControl_Zoom,1,"millimeters"},
    {ControlId::ExposureSeconds,true,CameraControl_Exposure,1,"seconds",true},
    {ControlId::ApertureFNumber,true,CameraControl_Iris,0.1,"f-number"},
    {ControlId::FocusDistanceMeters,true,CameraControl_Focus,0.001,"meters"},
    {ControlId::FocusMode,true,CameraControl_Focus,1,"",false,true},
    {ControlId::ExposureMode,true,CameraControl_Exposure,1,"",false,true},
    {ControlId::Brightness,false,VideoProcAmp_Brightness,1,"driver units"},
    {ControlId::Contrast,false,VideoProcAmp_Contrast,0.01,"gain factor"},
    {ControlId::Hue,false,VideoProcAmp_Hue,0.01,"degrees"},
    {ControlId::Saturation,false,VideoProcAmp_Saturation,1,"driver units"},
    {ControlId::Sharpness,false,VideoProcAmp_Sharpness,1,"driver units"},
    {ControlId::Gamma,false,VideoProcAmp_Gamma,0.01,"gamma"},
    {ControlId::Gain,false,VideoProcAmp_Gain,1,"driver units"},
    {ControlId::BacklightCompensation,false,VideoProcAmp_BacklightCompensation,1,"boolean"},
    {ControlId::WhiteBalanceKelvin,false,VideoProcAmp_WhiteBalance,1,"kelvin"},
    {ControlId::WhiteBalanceMode,false,VideoProcAmp_WhiteBalance,1,"",false,true}
};
const Mapping* mapping(ControlId id) {
    const auto it=std::find_if(std::begin(mappings),std::end(mappings),[&](const auto& item){return item.id==id;});
    return it==std::end(mappings)?nullptr:it;
}
HRESULT range(const Source& source,const Mapping& map,long& minimum,long& maximum,long& step,long& defaultValue,long& flags) {
    if(map.camera) return source.camera?source.camera->GetRange(map.property,&minimum,&maximum,&step,&defaultValue,&flags):E_NOINTERFACE;
    return source.proc?source.proc->GetRange(map.property,&minimum,&maximum,&step,&defaultValue,&flags):E_NOINTERFACE;
}
HRESULT read(const Source& source,const Mapping& map,long& value,long& flags) {
    if(map.camera) return source.camera?source.camera->Get(map.property,&value,&flags):E_NOINTERFACE;
    return source.proc?source.proc->Get(map.property,&value,&flags):E_NOINTERFACE;
}
std::vector<ControlCapability> capabilities(const Source& source) {
    std::vector<ControlCapability> result;
    for(const auto& map:mappings) {
        long lo{},hi{},step{},def{},flags{};if(FAILED(range(source,map,lo,hi,step,def,flags)) || lo>hi || step<0) continue;
        ControlCapability cap;cap.id=map.id;cap.unit=map.unit;cap.readable=true;
        cap.additional["MediaFoundation.property"]=std::to_string(map.property);
        cap.additional["MediaFoundation.interface"]=map.camera?"IAMCameraControl":"IAMVideoProcAmp";
        cap.additional["MediaFoundation.defaultRaw"]=std::to_string(def);
        if(map.mode) {
            cap.kind=ControlKind::Mode;
            if(flags&CameraControl_Flags_Manual) cap.modes.push_back(ControlMode::Manual);
            if(flags&CameraControl_Flags_Auto) cap.modes.push_back(ControlMode::Automatic);
            if(cap.modes.empty()) continue;cap.writable=true;
        } else if(map.exposure) {
            if(lo<-1022 || hi>1023 || hi-lo>4096) continue;
            NumericRange values{std::exp2(static_cast<double>(lo)),std::exp2(static_cast<double>(hi)),0,{}};
            for(long raw=lo;raw<=hi;raw+=std::max(step,1L)) values.discreteValues.push_back(std::exp2(static_cast<double>(raw)));
            cap.range=std::move(values);cap.writable=flags&CameraControl_Flags_Manual;
        } else {
            cap.range=NumericRange{lo*map.scale,hi*map.scale,step*map.scale,{}};cap.writable=flags&CameraControl_Flags_Manual;
        }
        result.push_back(std::move(cap));
    }
    return result;
}
Result<ControlValue> query(const Source& source,ControlId id) {
    const auto* map=mapping(id);if(!map) return std::unexpected(Error{ErrorCode::Unsupported,"No camera control mapping"});
    long value{},flags{};const auto hr=read(source,*map,value,flags);if(FAILED(hr)) return std::unexpected(error(hr,"Reading a camera control"));
    if(map->mode) return ControlValue{flags&CameraControl_Flags_Auto?ControlMode::Automatic:ControlMode::Manual};
    const double number=map->exposure?std::exp2(static_cast<double>(value)):value*map->scale;
    if(!std::isfinite(number)) return std::unexpected(Error{ErrorCode::BackendFailure,"Camera returned a nonfinite control value"});
    return ControlValue{number};
}
Result<ControlValue> apply(const Source& source,const ControlCommand& command) {
    const auto* map=mapping(command.id);if(!map) return std::unexpected(Error{ErrorCode::Unsupported,"No camera control mapping"});
    // Reuse core preflight with a fresh driver range before calling Set.
    ControlPort validation(capabilities(source),{{},[](const ControlCommand& value)->Result<ControlValue>{return value.value;}});
    if(auto valid=validation.apply(command);!valid) return std::unexpected(valid.error());
    long raw{},flags=CameraControl_Flags_Manual;
    if(map->mode) {
        long existingFlags{};const auto hr=read(source,*map,raw,existingFlags);
        if(FAILED(hr)) return std::unexpected(error(hr,"Reading the current control before setting its mode"));
        flags=std::get<ControlMode>(command.value)==ControlMode::Automatic?CameraControl_Flags_Auto:CameraControl_Flags_Manual;
    } else {
        const double value=std::get<double>(command.value);
        raw=static_cast<long>(std::llround(map->exposure?std::log2(value):value/map->scale));
    }
    const auto hr=map->camera?source.camera->Set(map->property,raw,flags):source.proc->Set(map->property,raw,flags);
    if(FAILED(hr)) return std::unexpected(error(hr,"Setting a camera control"));
    return query(source,command.id);
}
ControlPort port(const std::shared_ptr<Source>& source) {
    return ControlPort(capabilities(*source),{
        [source](ControlId id) {return source->owner->call([source,id]{return query(*source,id);});},
        [source](const ControlCommand& command) {return source->owner->call([source,command]{return apply(*source,command);});}});
}
void attributes(std::vector<NativeFeature>& features,IMFAttributes* values,const std::string& prefix) {
    UINT32 count{};if(FAILED(values->GetCount(&count))) return;
    for(UINT32 i=0;i<count;++i) {
        GUID key{};PROPVARIANT value{};const auto hr=values->GetItemByIndex(i,&key,&value);
        if(FAILED(hr)) continue;
        NativeFeature feature;feature.id=prefix+guid(key);feature.name=feature.id;feature.readable=true;
        feature.additional["MediaFoundation.variantType"]=std::to_string(value.vt);
        switch(value.vt) {
        case VT_UI4:feature.type=FeatureType::Integer;feature.value=FeatureValue{static_cast<std::int64_t>(value.ulVal)};break;
        case VT_UI8:
            feature.type=FeatureType::Integer;
            if(value.uhVal.QuadPart<=static_cast<ULONGLONG>(std::numeric_limits<std::int64_t>::max())) feature.value=FeatureValue{static_cast<std::int64_t>(value.uhVal.QuadPart)};
            else {feature.type=FeatureType::String;feature.value=FeatureValue{std::to_string(value.uhVal.QuadPart)};}break;
        case VT_R8:feature.type=FeatureType::Number;feature.value=FeatureValue{value.dblVal};break;
        case VT_LPWSTR:feature.type=FeatureType::String;feature.value=FeatureValue{value.pwszVal?utf8(value.pwszVal,wcslen(value.pwszVal)):std::string{}};break;
        case VT_CLSID:feature.type=FeatureType::String;feature.value=FeatureValue{value.puuid?guid(*value.puuid):std::string{}};break;
        case VT_VECTOR|VT_UI1: {
            feature.type=FeatureType::Bytes;std::vector<std::byte> bytes(value.caub.cElems);
            if(!bytes.empty()) std::memcpy(bytes.data(),value.caub.pElems,bytes.size());feature.value=FeatureValue{std::move(bytes)};break;
        }
        default:feature.value=std::unexpected(Error{ErrorCode::Unsupported,"This native attribute type is not representable"});break;
        }
        features.push_back(std::move(feature));PropVariantClear(&value);
    }
}
Result<CameraFeatures> describe(const std::shared_ptr<Source>& source,const FeatureQueryOptions& options) {
    CameraFeatures result;result.camera=source->info;auto& info=result.camera;
    ComPtr<IMFPresentationDescriptor> presentation;auto hr=source->media->CreatePresentationDescriptor(presentation.GetAddressOf());
    if(FAILED(hr)) return std::unexpected(error(hr,"Querying camera stream capabilities"));
    attributes(result.nativeFeatures,presentation.Get(),"MediaFoundation.presentation:");
    DWORD streams{};hr=presentation->GetStreamDescriptorCount(&streams);
    if(FAILED(hr)) return std::unexpected(error(hr,"Counting camera streams"));
    for(DWORD stream=0;stream<streams;++stream) {
        BOOL selected{};ComPtr<IMFStreamDescriptor> descriptor;
        if(FAILED(presentation->GetStreamDescriptorByIndex(stream,&selected,descriptor.GetAddressOf()))) continue;
        attributes(result.nativeFeatures,descriptor.Get(),"MediaFoundation.stream."+std::to_string(stream)+":");
        ComPtr<IMFMediaTypeHandler> handler;if(FAILED(descriptor->GetMediaTypeHandler(handler.GetAddressOf()))) continue;
        GUID major{};if(FAILED(handler->GetMajorType(&major)) || major!=MFMediaType_Video) continue;
        DWORD count{};if(FAILED(handler->GetMediaTypeCount(&count))) continue;
        for(DWORD index=0;index<count;++index) {
            ComPtr<IMFMediaType> media;if(FAILED(handler->GetMediaTypeByIndex(index,media.GetAddressOf()))) continue;
            CameraFormat format;format.id="MediaFoundation:"+std::to_string(stream)+":"+std::to_string(index);
            UINT32 width{},height{};
            if(FAILED(MFGetAttributeSize(media.Get(),MF_MT_FRAME_SIZE,&width,&height)) || !width || !height) continue;
            format.resolution={width,height};GUID subtype{};
            if(SUCCEEDED(media->GetGUID(MF_MT_SUBTYPE,&subtype))) format.additional["MediaFoundation.subtypeGUID"]=guid(subtype);
            UINT32 numerator{},denominator{};
            if(SUCCEEDED(MFGetAttributeRatio(media.Get(),MF_MT_FRAME_RATE,&numerator,&denominator)) && numerator && denominator) {
                const auto fps=static_cast<double>(numerator)/denominator;format.frameRates.push_back({fps,fps,0,{fps}});
            }
            // GUID subtypes remain native metadata rather than guessed codec names.
            attributes(result.nativeFeatures,media.Get(),"MediaFoundation.format."+std::to_string(stream)+"."+std::to_string(index)+":");
            info.formats.push_back(std::move(format));
        }
    }
    auto standard=queryCameraFeatures(std::move(info),port(source),options);
    standard.nativeFeatures=std::move(result.nativeFeatures);
    // Include all 7 camera properties and all 10 video-processing properties,
    // including ColorEnable, which has no standard ControlId.
    for(bool camera:{true,false}) for(long property=0;property<(camera?7:10);++property) {
        Mapping map{ControlId::Brightness,camera,property,1,"driver units"};long lo{},hi{},step{},def{},flags{};
        if(FAILED(range(*source,map,lo,hi,step,def,flags))) continue;
        NativeFeature feature;feature.id=std::string(camera?"IAMCameraControl:":"IAMVideoProcAmp:")+std::to_string(property);
        feature.name=feature.id;feature.readable=true;feature.writable=flags!=0;
        if(lo<=hi && step>=0) feature.range=NumericRange{static_cast<double>(lo),static_cast<double>(hi),static_cast<double>(step),{}};
        feature.additional["MediaFoundation.flags"]=std::to_string(flags);feature.additional["MediaFoundation.default"]=std::to_string(def);
        if(options.includeCurrentValues) {
            long raw{},currentFlags{};hr=read(*source,map,raw,currentFlags);
            feature.value=SUCCEEDED(hr)?Result<FeatureValue>{FeatureValue{static_cast<std::int64_t>(raw)}}:Result<FeatureValue>{std::unexpected(error(hr,"Reading native control"))};
            feature.additional["MediaFoundation.currentFlags"]=std::to_string(currentFlags);
        }
        standard.nativeFeatures.push_back(std::move(feature));
    }
    return standard;
}
}
bool NativeCameraSystem::available() const noexcept {return true;}
CameraPermission NativeCameraSystem::permissionStatus() const noexcept {return CameraPermission::NotDetermined;}
Result<std::vector<CameraInfo>> NativeCameraSystem::cameras() const {
    return worker()->call([]() -> Result<std::vector<CameraInfo>> {
        ActivationList list;const auto hr=enumerate(list);if(FAILED(hr)) return std::unexpected(error(hr,"Enumerating cameras"));
        std::vector<CameraInfo> result;for(UINT32 i=0;i<list.count;++i) result.push_back(identity(list.values[i]));return result;
    });
}
Result<ControlPort> NativeCameraSystem::controls(const std::string& id) const {
    auto source=openSource(id);if(!source) return std::unexpected(source.error());
    return (*source)->owner->call([source=*source]() -> Result<ControlPort> {return port(source);});
}
Result<CameraFeatures> NativeCameraSystem::features(const std::string& id,const FeatureQueryOptions& options) const {
    auto source=openSource(id);if(!source) return std::unexpected(source.error());
    return (*source)->owner->call([source=*source,options] {return describe(source,options);});
}
Result<CameraPairing> NativeCameraSystem::pair(const std::string& id,const PairingOptions& options) const {
    if(auto valid=wide(id);!valid) return std::unexpected(valid.error());
    if(options.method==PairingMethod::DeviceBond || !options.parameters.empty())
        return std::unexpected(Error{ErrorCode::Unsupported,"Media Foundation does not perform device bonding or accept pairing parameters"});
    if(options.method!=PairingMethod::Session) return std::unexpected(Error{ErrorCode::InvalidArgument,"Unknown pairing method"});
    auto source=openSource(id);if(!source) return std::unexpected(source.error());
    auto state=*source;
    auto report=state->owner->call([state] {return describe(state,FeatureQueryOptions{false});});
    if(!report) return std::unexpected(report.error());
    return CameraPairing::create({report->camera,options.method,{{"MediaFoundation.connection","activated source; capture not started"}}},{
        [state]() -> Result<PairingState> {return state->owner->call([state]() -> Result<PairingState> {
            if(!state->media) return PairingState::Unpaired;
            ActivationList list;auto hr=enumerate(list);if(FAILED(hr)) return std::unexpected(error(hr,"Checking the camera connection"));
            for(UINT32 i=0;i<list.count;++i) if(identity(list.values[i]).id==state->info.id) return PairingState::Ready;
            return PairingState::Disconnected;
        });},
        [state]() -> Result<ControlPort> {return state->owner->call([state]() -> Result<ControlPort> {return port(state);});},
        [state]() -> Result<void> {return state->owner->call([state]() -> Result<void> {
            if(!state->media) return {};const auto hr=state->media->Shutdown();
            if(FAILED(hr)) return std::unexpected(error(hr,"Releasing the camera source"));
            state->camera.Reset();state->proc.Reset();state->media.Reset();return {};
        });},
        [state](const FeatureQueryOptions& query) {return state->owner->call([state,query] {return describe(state,query);});}});
}
} // namespace iiCameraIntergration
