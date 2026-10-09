#include <iiCameraIntergration/iiCameraIntergration.h>
#include <iiCameraIntergration/Camera.h>
#include <iiCameraIntergration/Media.h>
#include <iiCameraIntergration/NativeCamera.h>

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
    std::cout << "Installed iiCameraIntergration C++23 consumer passed\n";
    return 0;
}
