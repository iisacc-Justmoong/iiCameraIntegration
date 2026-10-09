#include <iiCameraIntergration/Camera.h>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

using namespace iiCameraIntergration;

namespace {
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error( \
    std::string("Check failed: ") + #expression + " at line " + std::to_string(__LINE__)); } while (false)

template<class T>
void expectError(const Result<T>& result, ErrorCode code) {
    CHECK(!result);
    CHECK(result.error().code == code);
    CHECK(!result.error().message.empty());
}

bool near(double actual, double expected, double tolerance = 1e-10) {
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

CameraInfo cameraFixture() {
    CameraInfo camera;
    camera.id = "test:camera-1";
    camera.name = "Interchangeable test camera";
    camera.backend = "test-provider";
    camera.manufacturer = "Example";
    camera.model = "Test body";
    camera.serialNumber = "body-123";
    camera.firmwareVersion = "1.2.3";
    camera.position = Position::External;
    camera.transport = Transport::Usb;
    SensorInfo sensor;
    sensor.widthMm = 36;
    sensor.heightMm = 24;
    sensor.cropFactor = 1;
    sensor.resolution = Size{6000, 4000};
    sensor.bitDepth = 14;
    sensor.colorFilterArray = "RGGB";
    sensor.additional["test-provider.sensor.unknown"] = "unmodeled sensor property";
    camera.sensor = std::move(sensor);
    LensInfo lens;
    lens.id = "test:lens-1";
    lens.manufacturer = "Example";
    lens.model = "24-70mm";
    lens.mount = "Test mount";
    lens.serialNumber = "lens-123";
    lens.projection = Projection::Rectilinear;
    lens.focalLengthMm = NumericRange{24, 70, 0, {}};
    lens.apertureFNumber = NumericRange{2.8, 22, 0, {}};
    lens.focusDistanceMeters = NumericRange{0.2, 100, 0, {}};
    lens.currentFocalLengthMm = 50;
    lens.currentApertureFNumber = 5.6;
    lens.horizontalFieldOfViewDegrees = 39.59775270904986;
    lens.interchangeable = true;
    lens.additional["test-provider.lens.unknown"] = "unmodeled lens property";
    camera.lenses.push_back(std::move(lens));
    CameraFormat format;
    format.id = "test:format-1";
    format.pixelFormat = "RGB24";
    format.codec = "rawvideo";
    format.resolution = {1920, 1080};
    format.frameRates = {{24, 60, 0, {24, 25, 30, 50, 60}}};
    format.horizontalFieldOfViewDegrees = 39.59775270904986;
    format.hdrSupported = true;
    format.additional["test-provider.format.unknown"] = "unmodeled format property";
    camera.formats.push_back(std::move(format));
    camera.activeFormatId = "test:format-1";
    ControlCapability control;
    control.id = ControlId::ZoomFactor;
    control.unit = "ratio";
    control.readable = control.writable = true;
    control.range = NumericRange{1, 8, 0.5, {}};
    control.additional["test-provider.control.unknown"] = "unmodeled control property";
    camera.controls.push_back(std::move(control));
    camera.additional["test-provider.camera.unknown"] = "unmodeled camera property";
    return camera;
}

void metadataPreservation() {
    auto camera = cameraFixture();
    CHECK(validateCameraInfo(camera));
    CHECK(camera.sensor->additional.at("test-provider.sensor.unknown") == "unmodeled sensor property");
    CHECK(camera.lenses.front().additional.at("test-provider.lens.unknown") == "unmodeled lens property");
    CHECK(camera.formats.front().additional.at("test-provider.format.unknown") == "unmodeled format property");
    CHECK(camera.controls.front().additional.at("test-provider.control.unknown") == "unmodeled control property");
    CHECK(camera.additional.at("test-provider.camera.unknown") == "unmodeled camera property");
    CHECK(camera.id == "test:camera-1" && camera.model == "Test body");
    CHECK(camera.sensor->resolution->width == 6000 && camera.sensor->bitDepth == 14);
    CHECK(camera.lenses.front().currentApertureFNumber == 5.6);
    CHECK(camera.formats.front().hdrSupported);
    CHECK(camera.activeFormatId == "test:format-1");
    auto copied = camera;
    copied.additional["test-provider.camera.unknown"] = "changed";
    copied.lenses.front().additional.clear();
    CHECK(camera.additional.at("test-provider.camera.unknown") == "unmodeled camera property");
    CHECK(camera.lenses.front().additional.size() == 1);
    auto capabilities = camera.controls;
    ControlPort port(capabilities, {});
    capabilities.front().additional.clear();
    capabilities.front().range->maximum = 2;
    CHECK(port.capabilities().front().additional.at("test-provider.control.unknown") == "unmodeled control property");
    CHECK(port.capabilities().front().range->maximum == 8);
    CameraInfo minimal;
    minimal.id = "minimal";
    minimal.name = "Camera with unreported capabilities";
    minimal.backend = "test-provider";
    CHECK(validateCameraInfo(minimal));
    CHECK(!minimal.sensor && minimal.formats.empty() && minimal.lenses.empty());
}

template<class Mutator>
void invalidMetadata(Mutator mutation) {
    auto camera = cameraFixture();
    mutation(camera);
    expectError(validateCameraInfo(camera), ErrorCode::InvalidArgument);
}

void metadataValidation() {
    invalidMetadata([](auto& c) { c.id.clear(); });
    invalidMetadata([](auto& c) { c.name.clear(); });
    invalidMetadata([](auto& c) { c.backend.clear(); });
    invalidMetadata([](auto& c) { c.id = std::string("camera\0id", 9); });
    invalidMetadata([](auto& c) { c.sensor->widthMm = 0; });
    invalidMetadata([](auto& c) { c.sensor->heightMm = -1; });
    invalidMetadata([](auto& c) { c.sensor->cropFactor = std::numeric_limits<double>::infinity(); });
    invalidMetadata([](auto& c) { c.sensor->resolution->height = 0; });
    invalidMetadata([](auto& c) { c.sensor->bitDepth = 0; });
    invalidMetadata([](auto& c) { c.lenses.push_back(c.lenses.front()); });
    invalidMetadata([](auto& c) { c.lenses.front().id.clear(); });
    invalidMetadata([](auto& c) { c.lenses.front().focalLengthMm->maximum = 10; });
    invalidMetadata([](auto& c) { c.lenses.front().focalLengthMm->minimum = 0; });
    invalidMetadata([](auto& c) { c.lenses.front().apertureFNumber->minimum = 0; });
    invalidMetadata([](auto& c) { c.lenses.front().apertureFNumber->minimum = -1; });
    invalidMetadata([](auto& c) { c.lenses.front().focusDistanceMeters->step = -1; });
    invalidMetadata([](auto& c) { c.lenses.front().currentFocalLengthMm = std::numeric_limits<double>::quiet_NaN(); });
    invalidMetadata([](auto& c) { c.lenses.front().currentApertureFNumber = 0; });
    invalidMetadata([](auto& c) { c.lenses.front().currentFocalLengthMm = 71; });
    invalidMetadata([](auto& c) { c.lenses.front().currentApertureFNumber = 1.4; });
    invalidMetadata([](auto& c) { c.lenses.front().horizontalFieldOfViewDegrees = 360; });
    invalidMetadata([](auto& c) { c.formats.push_back(c.formats.front()); });
    invalidMetadata([](auto& c) { c.formats.front().id.clear(); });
    invalidMetadata([](auto& c) { c.formats.front().resolution.width = 0; });
    invalidMetadata([](auto& c) { c.formats.front().frameRates.front().minimum = 0; });
    invalidMetadata([](auto& c) { c.formats.front().frameRates.front().discreteValues.push_back(120); });
    invalidMetadata([](auto& c) { c.formats.front().horizontalFieldOfViewDegrees = -1; });
    invalidMetadata([](auto& c) { c.activeFormatId = "unknown-format"; });
    invalidMetadata([](auto& c) { c.controls.push_back(c.controls.front()); });
    invalidMetadata([](auto& c) { c.controls.front().range->step = std::numeric_limits<double>::quiet_NaN(); });
    invalidMetadata([](auto& c) { c.controls.front().kind = ControlKind::Mode; });
    invalidMetadata([](auto& c) { c.controls.front().kind = ControlKind::Identifier; });
    invalidMetadata([](auto& c) { c.controls.front().kind = ControlKind::Identifier; c.controls.front().identifiers = {std::string("a\0b", 3)}; });
    auto disconnected = cameraFixture();
    disconnected.connected = false;
    CHECK(validateCameraInfo(disconnected));
    disconnected.lenses.front().focusDistanceMeters->minimum = 0;
    CHECK(validateCameraInfo(disconnected));
}

void numericRanges() {
    NumericRange continuous{-2, 2, 0, {}};
    CHECK(continuous.contains(-2) && continuous.contains(2) && continuous.contains(0.123456));
    CHECK(!continuous.contains(-2.01) && !continuous.contains(2.01));
    CHECK(!continuous.contains(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!continuous.contains(std::numeric_limits<double>::infinity()));
    NumericRange stepped{0.1, 1.1, 0.2, {}};
    CHECK(stepped.contains(0.1) && stepped.contains(0.3) && stepped.contains(1.1));
    CHECK(!stepped.contains(0.2) && !stepped.contains(1.0));
    NumericRange discrete{1, 8, 2, {1, 2.8, 8}};
    CHECK(discrete.contains(1) && discrete.contains(2.8) && discrete.contains(8));
    CHECK(!discrete.contains(3) && !discrete.contains(5));
    NumericRange point{4, 4, 0, {}};
    CHECK(point.contains(4) && !point.contains(4.001));
    NumericRange largeStepCount{0, 1e12, 1, {}};
    CHECK(largeStepCount.contains(999999999999.0));
    CHECK(!largeStepCount.contains(999999999999.5));
    for (const auto& invalid : std::array<NumericRange, 6>{
        NumericRange{2, 1, 0, {}}, NumericRange{0, 2, -1, {}},
        NumericRange{0, 2, std::numeric_limits<double>::infinity(), {}},
        NumericRange{0, std::numeric_limits<double>::infinity(), 0, {}},
        NumericRange{0, 2, 0, {3}},
        NumericRange{0, 2, 0, {std::numeric_limits<double>::quiet_NaN()}}}) {
        CHECK(!invalid.contains(1));
    }
}

std::vector<ControlCapability> controlFixture() {
    auto controls = cameraFixture().controls;
    ControlCapability mode;
    mode.id = ControlId::FocusMode;
    mode.kind = ControlKind::Mode;
    mode.readable = mode.writable = true;
    mode.modes = {ControlMode::Manual, ControlMode::Continuous};
    controls.push_back(mode);
    ControlCapability identifier;
    identifier.id = ControlId::Lens;
    identifier.kind = ControlKind::Identifier;
    identifier.readable = identifier.writable = true;
    identifier.identifiers = {"wide", "tele"};
    controls.push_back(identifier);
    ControlCapability readOnly;
    readOnly.id = ControlId::ExposureSeconds;
    readOnly.readable = true;
    readOnly.range = NumericRange{0.001, 1, 0, {}};
    controls.push_back(readOnly);
    ControlCapability writeOnly;
    writeOnly.id = ControlId::Iso;
    writeOnly.writable = true;
    writeOnly.range = NumericRange{100, 6400, 100, {}};
    controls.push_back(writeOnly);
    return controls;
}

void controlPreflight() {
    int queries = 0, commands = 0;
    ControlPort port(controlFixture(), {
        [&](ControlId) -> Result<ControlValue> { ++queries; return ControlValue{2.0}; },
        [&](const ControlCommand& command) -> Result<ControlValue> { ++commands; return command.value; }
    });
    expectError(port.query(ControlId::ApertureFNumber), ErrorCode::Unsupported);
    expectError(port.query(ControlId::Iso), ErrorCode::Unsupported);
    CHECK(queries == 0);
    expectError(port.apply({ControlId::ApertureFNumber, 4.0}), ErrorCode::Unsupported);
    expectError(port.apply({ControlId::ExposureSeconds, 0.01}), ErrorCode::Unsupported);
    expectError(port.apply({ControlId::ZoomFactor, 0.5}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::ZoomFactor, 8.5}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::ZoomFactor, 1.25}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::ZoomFactor, std::numeric_limits<double>::quiet_NaN()}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::ZoomFactor, std::numeric_limits<double>::infinity()}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::ZoomFactor, ControlMode::Manual}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::ZoomFactor, std::string("2")}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::FocusMode, ControlMode::Automatic}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::FocusMode, 1.0}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::Lens, std::string("unknown")}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::Lens, std::string{}}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::Lens, std::string("wide\0", 5)}), ErrorCode::InvalidArgument);
    expectError(port.apply({ControlId::Lens, 1.0}), ErrorCode::InvalidArgument);
    CHECK(commands == 0);
    CHECK(port.query(ControlId::ZoomFactor));
    CHECK(port.apply({ControlId::ZoomFactor, 2.5}));
    CHECK(port.apply({ControlId::FocusMode, ControlMode::Continuous}));
    CHECK(port.apply({ControlId::Lens, std::string("tele")}));
    CHECK(port.apply({ControlId::Iso, 400.0}));
    CHECK(queries == 1 && commands == 4);
    ControlPort absent(controlFixture(), {});
    expectError(absent.query(ControlId::ZoomFactor), ErrorCode::Unavailable);
    expectError(absent.apply({ControlId::ZoomFactor, 2.0}), ErrorCode::Unavailable);
    expectError(absent.apply({ControlId::ZoomFactor, 0.0}), ErrorCode::InvalidArgument);
}

void providerContract() {
    struct ProviderState { int queries{}, commands{}; };
    std::weak_ptr<ProviderState> weak;
    {
        auto state = std::make_shared<ProviderState>();
        weak = state;
        ControlPort port(controlFixture(), {
            [state](ControlId id) -> Result<ControlValue> {
                ++state->queries;
                CHECK(id == ControlId::ZoomFactor);
                return ControlValue{3.0};
            },
            [state](const ControlCommand& command) -> Result<ControlValue> {
                ++state->commands;
                CHECK(command.id == ControlId::ZoomFactor);
                CHECK(std::get<double>(command.value) == 2.0);
                return ControlValue{2.5};
            }
        });
        state.reset();
        CHECK(!weak.expired());
        const auto queried = port.query(ControlId::ZoomFactor);
        CHECK(queried && std::get<double>(*queried) == 3.0);
        const auto applied = port.apply({ControlId::ZoomFactor, 2.0});
        CHECK(applied && std::get<double>(*applied) == 2.5);
        auto copied = port;
        CHECK(copied.query(ControlId::ZoomFactor));
        CHECK(weak.lock()->queries == 2 && weak.lock()->commands == 1);
    }
    CHECK(weak.expired());
    const Error backendError{ErrorCode::PermissionDenied, "Permission revoked by device", 731};
    ControlPort errors(controlFixture(), {
        [backendError](ControlId) -> Result<ControlValue> { return std::unexpected(backendError); },
        [backendError](const ControlCommand&) -> Result<ControlValue> { return std::unexpected(backendError); }
    });
    for (const auto& result : {errors.query(ControlId::ZoomFactor), errors.apply({ControlId::ZoomFactor, 2.0})}) {
        expectError(result, ErrorCode::PermissionDenied);
        CHECK(result.error().nativeCode == 731);
        CHECK(result.error().message == backendError.message);
    }
    ControlPort exceptions(controlFixture(), {
        [](ControlId) -> Result<ControlValue> { throw std::runtime_error("query failure"); },
        [](const ControlCommand&) -> Result<ControlValue> { throw std::runtime_error("command failure"); }
    });
    const auto queryException = exceptions.query(ControlId::ZoomFactor);
    const auto commandException = exceptions.apply({ControlId::ZoomFactor, 2.0});
    expectError(queryException, ErrorCode::BackendFailure);
    expectError(commandException, ErrorCode::BackendFailure);
    CHECK(queryException.error().message == "query failure");
    CHECK(commandException.error().message == "command failure");
    ControlPort unknownExceptions(controlFixture(), {
        [](ControlId) -> Result<ControlValue> { throw 17; },
        [](const ControlCommand&) -> Result<ControlValue> { throw 29; }
    });
    expectError(unknownExceptions.query(ControlId::ZoomFactor), ErrorCode::BackendFailure);
    expectError(unknownExceptions.apply({ControlId::ZoomFactor, 2.0}), ErrorCode::BackendFailure);
}

void optics() {
    // Independent reference values for a 36x24 mm sensor and 50 mm rectilinear lens.
    const auto fullFrame = rectilinearFieldOfView(36, 24, 50);
    CHECK(fullFrame);
    CHECK(near(fullFrame->horizontalDegrees, 39.59775270904986));
    CHECK(near(fullFrame->verticalDegrees, 26.991466561591622));
    CHECK(near(fullFrame->diagonalDegrees, 46.79300334396557));
    const auto inverse = focalLengthForHorizontalFieldOfView(36, fullFrame->horizontalDegrees);
    CHECK(inverse && near(*inverse, 50));
    const auto ninety = focalLengthForHorizontalFieldOfView(36, 90);
    CHECK(ninety && near(*ninety, 18));
    const auto sixty = focalLengthForHorizontalFieldOfView(36, 60);
    CHECK(sixty && near(*sixty, 31.17691453623979));
    const auto scaled = rectilinearFieldOfView(18, 12, 25);
    CHECK(scaled && near(scaled->horizontalDegrees, fullFrame->horizontalDegrees));
    const auto rotated = rectilinearFieldOfView(24, 36, 50);
    CHECK(rotated && near(rotated->horizontalDegrees, fullFrame->verticalDegrees));
    CHECK(near(rotated->diagonalDegrees, fullFrame->diagonalDegrees));
    for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        expectError(rectilinearFieldOfView(invalid, 24, 50), ErrorCode::InvalidArgument);
        expectError(rectilinearFieldOfView(36, invalid, 50), ErrorCode::InvalidArgument);
        expectError(rectilinearFieldOfView(36, 24, invalid), ErrorCode::InvalidArgument);
        expectError(focalLengthForHorizontalFieldOfView(invalid, 90), ErrorCode::InvalidArgument);
        expectError(focalLengthForHorizontalFieldOfView(36, invalid), ErrorCode::InvalidArgument);
    }
    expectError(focalLengthForHorizontalFieldOfView(36, 180), ErrorCode::InvalidArgument);
    expectError(focalLengthForHorizontalFieldOfView(36, 360), ErrorCode::InvalidArgument);
    expectError(rectilinearFieldOfView(1e308, 1e308, 1e-308), ErrorCode::InvalidArgument);
    expectError(focalLengthForHorizontalFieldOfView(1e308, 1e-308), ErrorCode::InvalidArgument);
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: camera_tests metadata_preservation|metadata_validation|numeric_ranges|control_preflight|provider_contract|optics\n";
        return 2;
    }
    try {
        const std::string_view name(argv[1]);
        if (name == "metadata_preservation") metadataPreservation();
        else if (name == "metadata_validation") metadataValidation();
        else if (name == "numeric_ranges") numericRanges();
        else if (name == "control_preflight") controlPreflight();
        else if (name == "provider_contract") providerContract();
        else if (name == "optics") optics();
        else throw std::runtime_error("Unknown test case: " + std::string(name));
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
