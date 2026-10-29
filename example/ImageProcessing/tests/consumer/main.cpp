#include <iiCameraIntegration/ImageProcessing.h>
#include <iiGraphicAdjust/RawProcessing.h>
#include <array>
int main() {
    iiCameraIntegration::RgbFrame frame;
    frame.dimensions={1,1}; frame.rowStride=3; frame.pixels={255,0,0};
    frame.colorSpace="gbr"; frame.colorRange="pc"; frame.colorTransfer="iec61966-2-1"; frame.colorPrimaries="bt709";
    const auto photo=iiCameraIntegration::processPhoto(frame);
    if (!photo || photo->image.pixels[0].r<.99F || photo->histogram.pixelCount!=1) return 1;
    const std::array<std::uint16_t,4> mosaic{65535,0,0,0};
    const auto raw=iiGraphicAdjust::developRaw({2,2,2,mosaic});
    if (!raw || raw->pixels[0].r!=1.F) return 2;
    return 0;
}
