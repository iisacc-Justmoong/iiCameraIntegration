#include <iiCameraIntegration/iiCameraIntegration.h>
#include <iostream>
using namespace iiCameraIntegration;
int main() {
    auto codecs=codecCatalog();
    auto formats=formatCatalog();
    auto probe=probeMedia(MediaSource{"unavailable.mov"});
    auto packets=PacketCapture::open(MediaSource{"0"});
    auto frame=decodeRgbFrame(MediaSource{"unavailable.mov"});
    auto recording=recordMedia(MediaSource{"0"},"unavailable.nut",RecordingFilePort{});
    if (codecs || formats || probe || packets || frame || recording) return 1;
    for (const auto* error : {&codecs.error(),&formats.error(),&probe.error(),&packets.error(),&frame.error(),&recording.error()}) {
        if (error->code != ErrorCode::Unavailable) {std::cerr << error->message << '\n'; return 1;}
    }
    return rectilinearFieldOfView(36,24,50) ? 0 : 1;
}
