#include "../tools/FeatureJson.h"
#include <iostream>
using namespace iiCameraIntegration;
int main() {
    CameraFeatures result;
    result.camera.id="fixture:camera";result.camera.name="Camera \"A\"\n한국어 😀";result.camera.backend="fixture";
    result.camera.additional["path"]="C:\\camera";
    result.camera.controls.push_back({.id=ControlId::Brightness,.readable=true,.range=NumericRange{0,100,1,{}}});
    result.controls.push_back({result.camera.controls[0],std::unexpected(Error{ErrorCode::Busy,"try\tagain"})});
    result.nativeFeatures.push_back({.id="fixture:bytes",.name="Payload",.type=FeatureType::Bytes,
        .readable=true,.value=FeatureValue{std::vector<std::byte>{std::byte{0},std::byte{255}}}});
    result.nativeFeatures.push_back({.id="fixture:integer",.name="Counter",.readable=true,
        .value=FeatureValue{INT64_C(9007199254740993)}});
    cli::report(std::cout,{{result.camera.id,result},{"fixture:missing",std::unexpected(Error{ErrorCode::NotFound,"Disconnected"})}});
}
