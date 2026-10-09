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
