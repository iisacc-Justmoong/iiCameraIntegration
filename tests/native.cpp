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
        const auto paired = system.pair("unavailable-camera");
        require(!paired && paired.error().code == ErrorCode::Unavailable, "unavailable adapter pairing");
        const auto discovered = system.devices().discover();
        require(!discovered && discovered.error().code == ErrorCode::Unavailable, "unavailable device port discovery");
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
        require(camera.backend == "AVFoundation" || camera.backend == "V4L2" ||
                camera.backend == "MediaFoundation" || camera.backend == "Camera2", "native backend identifier");
        formatCount += camera.formats.size();
        auto controls = system.controls(camera.id);
        if (!controls) {
            // Device hot-unplug between discovery and control creation is legal.
            require(controls.error().code == ErrorCode::NotFound, "control creation for connected camera");
            continue;
        }
        const auto physicalFocus = controls->query(ControlId::FocusDistanceMeters);
        if(camera.backend=="AVFoundation" || camera.backend=="V4L2" || camera.backend=="Camera2")
            require(!physicalFocus && physicalFocus.error().code == ErrorCode::Unsupported,
                    "native lens position is not fabricated as physical focus distance");
        const auto unsupported = controls->apply({ControlId::Lens, std::string("nonexistent-lens")});
        require(!unsupported && unsupported.error().code == ErrorCode::Unsupported,
                "unsupported lens mutation is rejected without configuring hardware");
        if (camera.backend=="AVFoundation" && before != CameraPermission::Authorized) {
            const auto paired = system.pair(camera.id);
            require(!paired && (paired.error().code == ErrorCode::PermissionDenied ||
                               paired.error().code == ErrorCode::NotFound),
                    "pairing requires existing permission before opening an input");
        }
    }
    const auto empty = system.controls("");
    require(!empty && empty.error().code == ErrorCode::InvalidArgument, "empty device identifier");
    const auto embeddedNull = system.controls(std::string("camera\0suffix", 13));
    require(!embeddedNull && embeddedNull.error().code == ErrorCode::InvalidArgument, "embedded null device identifier");
    const auto missing = system.controls("iiCameraIntergration.test.absent.device.7e2fef72");
    require(!missing && missing.error().code == ErrorCode::NotFound, "unknown camera identifier");
    const auto invalidUtf8 = system.controls(std::string("\xff", 1));
    require(!invalidUtf8 && invalidUtf8.error().code == ErrorCode::InvalidArgument, "invalid UTF-8 control identifier");
    const auto emptyPair = system.pair("");
    require(!emptyPair && emptyPair.error().code == ErrorCode::InvalidArgument, "empty pairing identifier");
    const auto nullPair = system.pair(std::string("camera\0suffix", 13));
    require(!nullPair && nullPair.error().code == ErrorCode::InvalidArgument, "embedded null pairing identifier");
    const auto utf8Pair = system.pair(std::string("\xff", 1));
    require(!utf8Pair && utf8Pair.error().code == ErrorCode::InvalidArgument, "invalid UTF-8 pairing identifier");
    const auto absentPair = system.pair("iiCameraIntergration.test.absent.device.7e2fef72");
    require(!absentPair && absentPair.error().code == ErrorCode::NotFound, "pairing unknown camera identifier");
    PairingOptions bond;
    bond.method = PairingMethod::DeviceBond;
    const auto unsupportedBond = system.pair("iiCameraIntergration.test.absent.device.7e2fef72", bond);
    require(!unsupportedBond && unsupportedBond.error().code == ErrorCode::Unsupported, "native hardware bonding is explicit unsupported");
    PairingOptions parameters;
    parameters.parameters["credential"] = "test-only";
    const auto unsupportedParameters = system.pair("iiCameraIntergration.test.absent.device.7e2fef72", parameters);
    require(!unsupportedParameters && unsupportedParameters.error().code == ErrorCode::Unsupported, "native pairing rejects provider parameters");
    PairingOptions unknownMethod;
    unknownMethod.method = static_cast<PairingMethod>(-1);
    const auto invalidMethod = system.pair("iiCameraIntergration.test.absent.device.7e2fef72", unknownMethod);
    require(!invalidMethod && invalidMethod.error().code == ErrorCode::InvalidArgument, "invalid native pairing method");
    const auto discovered = system.devices().discover();
    require(discovered.has_value(), "native callback device port discovery");
    require(system.permissionStatus() == before, "discovery did not request or change camera permission");
    const auto features=system.features();
    require(features.has_value(),"bulk native feature discovery");
    std::set<std::string> featureIds;
    for(const auto& result:*features) {
        require(featureIds.insert(result.deviceId).second,"unique feature result IDs");
        if(result.features) {
            require(result.features->camera.id==result.deviceId,"feature report identifies exact device");
            require(validateCameraInfo(result.features->camera).has_value(),"feature metadata invariants");
        }
    }
    const auto invalidFeatures=system.devices().features(std::string("\xff",1));
    require(!invalidFeatures && invalidFeatures.error().code==ErrorCode::InvalidArgument,"invalid feature device ID");
    require(system.permissionStatus()==before,"feature query leaves authorization unchanged");
    std::cout << "Read-only native discovery: " << cameras->size() << " cameras, " << formatCount
              << " formats; invalid/missing pairing checks passed; authorization unchanged; no camera session started\n";
}
