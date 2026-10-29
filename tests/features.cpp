#include <iiCameraIntegration/Devices.h>
#include <iiCameraIntegration/NativeCamera.h>

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <condition_variable>
#include <thread>
#include <atomic>

using namespace iiCameraIntegration;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
CameraInfo fixture(std::string id = "fixture:camera") {
    CameraInfo camera;
    camera.id = std::move(id); camera.name = "Feature fixture"; camera.backend = "fixture";
    camera.controls = {
        {.id=ControlId::ZoomFactor, .readable=true, .writable=true, .range=NumericRange{1,8,0,{}}},
        {.id=ControlId::Iso, .readable=true, .writable=true, .range=NumericRange{50,3200,0,{}}},
        {.id=ControlId::LensPosition, .readable=false, .writable=true, .range=NumericRange{0,1,0,{}}}};
    return camera;
}
void bulk() {
    auto camera = fixture();
    int reads=0;
    ControlPort port(camera.controls, {[&](ControlId id) -> Result<ControlValue> {
        ++reads;
        if (id==ControlId::Iso) return std::unexpected(Error{ErrorCode::Busy,"Auto exposure is changing"});
        return 2.0;
    }, {}});
    const auto values=port.queryAll();
    CHECK(values.size()==3 && reads==2);
    CHECK(values[0].value && std::get<double>(*values[0].value)==2);
    CHECK(!values[1].value && values[1].value.error().code==ErrorCode::Busy);
    CHECK(!values[2].value && values[2].value.error().code==ErrorCode::Unsupported);
    auto report=queryCameraFeatures(camera,port);
    CHECK(report.controls.size()==3 && report.camera.id==camera.id);
    FeatureQueryOptions metadataOnly; metadataOnly.includeCurrentValues=false;
    const auto count=reads;
    report=queryCameraFeatures(camera,port,metadataOnly);
    CHECK(reads==count && report.controls.empty() && report.camera.controls.size()==3);
    auto absent=queryCameraFeatures(camera,std::unexpected(Error{ErrorCode::PermissionDenied,"No access"}));
    CHECK(absent.controls.size()==3 && absent.controls[0].value.error().code==ErrorCode::PermissionDenied);
    CHECK(absent.controls[2].value.error().code==ErrorCode::Unsupported);
    ControlPort invalid(camera.controls,{[](ControlId) -> Result<ControlValue> {return std::string("incorrect native type");},{}});
    const auto malformed=invalid.queryAll();
    CHECK(!malformed[0].value && malformed[0].value.error().code==ErrorCode::BackendFailure);
    ControlPort throwing(camera.controls,{[](ControlId) -> Result<ControlValue> {throw std::runtime_error("driver failure");},{}});
    const auto failed=throwing.queryAll();CHECK(failed.size()==3 && failed[0].value.error().code==ErrorCode::BackendFailure);
}
void devices() {
    int discoveries=0,reads=0;
    auto first=fixture("fixture:one"),second=fixture("fixture:two");
    CameraDeviceSystem system({
        [&]() -> Result<std::vector<CameraInfo>> {++discoveries; return std::vector{first,second};}, {},
        [&](const std::string& id) -> Result<ControlPort> {
            if (id==second.id) return std::unexpected(Error{ErrorCode::NotFound,"Unplugged after discovery"});
            return ControlPort(first.controls,{[&](ControlId) -> Result<ControlValue> {++reads; return 2.0;},{}});
        }, {}});
    auto all=system.features();
    CHECK(all && all->size()==2 && discoveries==1 && reads==2);
    CHECK((*all)[0].features && (*all)[1].features);
    CHECK((*all)[1].features->controls[0].value.error().code==ErrorCode::NotFound);
    auto single=system.features(first.id); CHECK(single && single->camera.id==first.id);
    auto missing=system.features("fixture:missing"); CHECK(!missing && missing.error().code==ErrorCode::NotFound);
    auto invalid=system.features(std::string("\xff",1)); CHECK(!invalid && invalid.error().code==ErrorCode::InvalidArgument);
    CameraDeviceSystem extensions({[&]() -> Result<std::vector<CameraInfo>> {return std::vector{first,second};}, {}, {},
        [&](const std::string& id,const FeatureQueryOptions&) -> Result<CameraFeatures> {
            if(id==second.id) throw std::runtime_error("A vendor query failed");
            CameraFeatures result; result.camera=first;
            result.nativeFeatures.push_back({.id="fixture:vendor", .name="Vendor switch", .type=FeatureType::Boolean,
                .readable=true, .value=FeatureValue{true}});
            return result;
        }});
    all=extensions.features(); CHECK(all && (*all)[0].features && !(*all)[1].features);
    CHECK((*all)[0].features->nativeFeatures[0].value && std::get<bool>(**(*all)[0].features->nativeFeatures[0].value));
    CHECK((*all)[1].features.error().code==ErrorCode::BackendFailure);
    CameraDeviceSystem wrong({[&]() -> Result<std::vector<CameraInfo>> {return std::vector{first};},{},{},
        [&](const std::string&,const FeatureQueryOptions&) -> Result<CameraFeatures> {CameraFeatures result;result.camera=second;return result;}});
    single=wrong.features(first.id); CHECK(!single && single.error().code==ErrorCode::BackendFailure);
    CameraDeviceSystem duplicates({[&]() -> Result<std::vector<CameraInfo>> {return std::vector{first};},{},{},
        [&](const std::string&,const FeatureQueryOptions&) -> Result<CameraFeatures> {
            CameraFeatures result;result.camera=first;
            result.nativeFeatures={{.id="vendor:one",.name="One"},{.id="vendor:one",.name="Duplicate"}};return result;
        }});
    single=duplicates.features(first.id);CHECK(!single && single.error().code==ErrorCode::BackendFailure);
}
void lifecycle() {
    auto camera=fixture(); bool released=false; int reports=0;
    auto pairing=CameraPairing::create({camera,PairingMethod::Session,{}},{
        []() -> Result<PairingState> {return PairingState::Ready;},
        [camera]() -> Result<ControlPort> {return ControlPort(camera.controls,{[](ControlId)->Result<ControlValue>{return 2.0;},{}});},
        [&]() -> Result<void> {released=true;return {};},
        [&](const FeatureQueryOptions&) -> Result<CameraFeatures> {++reports;CameraFeatures result;result.camera=camera;return result;}});
    CHECK(pairing && pairing->features() && reports==1);
    CHECK(pairing->unpair() && released);
    auto report=pairing->features(); CHECK(!report && report.error().code==ErrorCode::Cancelled && reports==1);
}
void concurrency() {
    auto camera=fixture();std::mutex mutex;std::condition_variable changed;
    bool entered=false,finish=false;std::atomic<bool> releasing{},released{};
    auto pairing=CameraPairing::create({camera,PairingMethod::Session,{}},{
        []() -> Result<PairingState> {return PairingState::Ready;},
        [camera]() -> Result<ControlPort> {return ControlPort(camera.controls,{});},
        [&]() -> Result<void> {released=true;return {};},
        [&](const FeatureQueryOptions&) -> Result<CameraFeatures> {
            std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return finish;});
            CameraFeatures result;result.camera=camera;return result;
        }});
    CHECK(pairing);bool queried=false,unpaired=false;
    std::thread query([&]{queried=pairing->features().has_value();});
    {std::unique_lock lock(mutex);changed.wait(lock,[&]{return entered;});}
    std::thread unpair([&]{releasing=true;changed.notify_all();unpaired=pairing->unpair().has_value();});
    {std::unique_lock lock(mutex);changed.wait(lock,[&]{return releasing.load();});CHECK(!released);finish=true;}
    changed.notify_all();query.join();unpair.join();CHECK(queried && unpaired && released);
}
}
int main(int argc,char** argv) {
    try {
        const std::string_view mode=argc>1?argv[1]:"";
        if(mode=="bulk") bulk(); else if(mode=="devices") devices(); else if(mode=="lifecycle") lifecycle(); else if(mode=="concurrency") concurrency(); else return 2;
        std::cout << "Feature " << mode << " contracts passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
