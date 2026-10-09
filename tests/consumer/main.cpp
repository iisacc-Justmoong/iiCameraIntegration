#include <iiCameraIntergration/iiCameraIntergration.h>
#include <iiCameraIntergration/Camera.h>
#include <iiCameraIntergration/Media.h>
#include <iiCameraIntergration/NativeCamera.h>

#ifdef IICAMERA_CONSUMER_WITH_PROCESSING
#include <iiCameraIntergration/ImageProcessing.h>
#include <iiGraphicAdjust/RawProcessing.h>
#endif
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

int main() {
    const auto field = iiCameraIntergration::rectilinearFieldOfView(36, 24, 50);
    if (!field || std::abs(field->horizontalDegrees - 39.59775270904986) > 1e-10) {
        std::cerr << "Installed optical API returned an incorrect result\n";
        return 1;
    }
    const auto focal = iiCameraIntergration::focalLengthForHorizontalFieldOfView(36, 90);
    if (!focal || std::abs(*focal - 18) > 1e-10) {
        std::cerr << "Installed inverse optical API returned an incorrect result\n";
        return 1;
    }
    // Installed public APIs, with an artificial provider rather than hardware.
    using namespace iiCameraIntergration;
    CameraInfo device;
    device.id="consumer.camera"; device.name="Package fixture"; device.backend="consumer";
    device.controls.push_back({.id=ControlId::ZoomFactor,.kind=ControlKind::Numeric,
        .readable=true,.writable=true,.range=NumericRange{1,4,0,{}}});
    auto ready=std::make_shared<bool>(true);
    CameraDeviceSystem devices({
        [device]() -> Result<std::vector<CameraInfo>> {return std::vector{device};},
        [device,ready](const std::string&,const PairingOptions& options) -> Result<CameraPairing> {
            return CameraPairing::create({device,options.method,{}},{
                [ready]() -> Result<PairingState> {return *ready?PairingState::Ready:PairingState::Unpaired;},
                [device]() -> Result<ControlPort> {return ControlPort(device.controls,{
                    [](ControlId) -> Result<ControlValue> {return 2.0;},
                    [](const ControlCommand& command) -> Result<ControlValue> {return command.value;}});},
                [ready]() -> Result<void> {*ready=false;return {};}});
        }});
    auto discovered=devices.discover();
    auto paired=devices.pair(device.id);
    if(!discovered || discovered->size()!=1 || !paired || !paired->info()) return 4;
    auto pairedControls=paired->controls();
    if(!pairedControls || !pairedControls->apply({ControlId::ZoomFactor,3.0}) || !paired->unpair()) return 5;
    auto revoked=pairedControls->query(ControlId::ZoomFactor);
    auto status=paired->status();
    if(revoked || revoked.error().code!=ErrorCode::Cancelled || !status || *status!=PairingState::Unpaired) return 6;
    NativeCameraSystem native;
    auto nativeDiscovery=native.devices().discover();
    if(!nativeDiscovery && nativeDiscovery.error().code!=ErrorCode::Unavailable) return 7;
    auto absent=native.pair("iiCameraIntergration.consumer.absent.55cd76a3");
    if(absent || (absent.error().code!=ErrorCode::NotFound && absent.error().code!=ErrorCode::Unavailable)) return 8;
#ifdef IICAMERA_CONSUMER_WITH_PROCESSING
    iiCameraIntergration::RgbFrame frame;
    frame.dimensions={1,1};frame.rowStride=3;frame.pixels={255,0,0};
    frame.colorSpace="gbr";frame.colorRange="pc";frame.colorTransfer="iec61966-2-1";frame.colorPrimaries="bt709";
    const auto photo=iiCameraIntergration::processPhoto(frame);
    if(!photo || photo->image.pixels[0].r<.99F || photo->histogram.pixelCount!=1) return 2;
    const std::array<std::uint16_t,4> mosaic{65535,0,0,0};
    const auto raw=iiGraphicAdjust::developRaw({2,2,2,mosaic});
    if(!raw || raw->pixels[0].r!=1.F) return 3;
#endif
    std::cout << "Installed iiCameraIntergration C++23 consumer passed\n";
    return 0;
}
