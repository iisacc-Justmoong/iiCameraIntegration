#include <iiCameraIntergration/NativeCamera.h>

#include <cstdlib>
#include <iostream>
#include <set>

using namespace iiCameraIntergration;

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "native test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main() {
    NativeCameraSystem system;
    const auto before = system.permissionStatus();
    const auto cameras = system.cameras();
    if (!system.available()) {
        require(before == CameraPermission::Unavailable, "unavailable adapter permission");
        require(!cameras && cameras.error().code == ErrorCode::Unavailable, "unavailable adapter enumeration");
        const auto controls = system.controls("unavailable-camera");
        require(!controls && controls.error().code == ErrorCode::Unavailable, "unavailable adapter controls");
        std::cout << "Native adapter correctly reports unavailable\n";
        return EXIT_SUCCESS;
    }
    require(before != CameraPermission::Unavailable, "native permission status");
    if (!cameras) {
        std::cerr << cameras.error().message << '\n';
        require(false, "native enumeration should succeed without opening camera input");
    }
    std::set<std::string> identifiers;
    std::size_t formatCount = 0;
    for (const auto &camera : *cameras) {
        const auto valid = validateCameraInfo(camera);
        if (!valid) std::cerr << camera.name << ": " << valid.error().message << '\n';
        require(valid.has_value(), "OS metadata satisfies public metadata invariants");
        require(identifiers.insert(camera.id).second, "unique camera identifiers");
        require(camera.backend == "AVFoundation", "native backend identifier");
        formatCount += camera.formats.size();
        auto controls = system.controls(camera.id);
        if (!controls) {
            // Device hot-unplug between discovery and control creation is legal.
            require(controls.error().code == ErrorCode::NotFound, "control creation for connected camera");
            continue;
        }
        const auto physicalFocus = controls->query(ControlId::FocusDistanceMeters);
        require(!physicalFocus && physicalFocus.error().code == ErrorCode::Unsupported,
                "native lens position is not fabricated as physical focus distance");
        const auto unsupported = controls->apply({ControlId::Lens, std::string("nonexistent-lens")});
        require(!unsupported && unsupported.error().code == ErrorCode::Unsupported,
                "unsupported lens mutation is rejected without configuring hardware");
    }
    const auto empty = system.controls("");
    require(!empty && empty.error().code == ErrorCode::InvalidArgument, "empty device identifier");
    const auto embeddedNull = system.controls(std::string("camera\0suffix", 13));
    require(!embeddedNull && embeddedNull.error().code == ErrorCode::InvalidArgument, "embedded null device identifier");
    const auto missing = system.controls("iiCameraIntergration.test.absent.device.7e2fef72");
    require(!missing && missing.error().code == ErrorCode::NotFound, "unknown camera identifier");
    require(system.permissionStatus() == before, "discovery did not request or change camera permission");
    std::cout << "Read-only native discovery: " << cameras->size() << " cameras, " << formatCount
              << " formats; authorization unchanged; no camera session started\n";
}
