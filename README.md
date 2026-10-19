# iiCameraIntergration

[Windows 빌드 및 실행](docs/WindowsBuildAndRun.md)

Package release: **0.4.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


Version **0.4.1**. A C++23 camera integration SDK for camera metadata, capability-aware optical
controls, media capture, and codec/container identification. The package and
namespace preserve the requested spelling **iiCameraIntergration**.
The public API uses standard C++ values and `std::expected`; it has no Qt dependency.
Native platform code is isolated in an optional adapter.

| Requirement | API | Implemented behavior |
| --- | --- | --- |
| Camera metadata | `CameraInfo`, `SensorInfo`, `LensInfo`, `CameraFormat` | Device identity, sensor/lens fields, format/frame-rate capabilities, active format, controls, and provider extension metadata. Unknown measurements remain optional. |
| Bulk feature inspection | `ControlPort::queryAll`, `CameraDeviceSystem::features`, `NativeCameraSystem::features`, `CameraPairing::features` | Single-device or all-device reports with capabilities, current values, native/vendor features and per-item errors; metadata-only mode avoids current-value reads. |
| Lens, field of view, variable aperture | `ControlPort`, `ControlCapability`, `ControlCommand` | Typed read/write commands with supported units, ranges, steps, modes, and identifiers. Rejects unsupported/read-only controls before dispatch. Includes rectilinear optical conversion functions. |
| Camera media capture | `PacketCapture`, `decodeRgbFrame`, `recordMedia` | FFmpeg device/file/network input, timestamped stream packets, decoded RGB still frames, and bounded recordings by remuxing compatible streams. |
| Codec/format recognition | `codecCatalog`, `formatCatalog`, `probeMedia`, `probeMediaBytes` | All codec descriptors and input/output formats registered in the linked FFmpeg build. Content probing identifies container and each stream codec independently of its extension; codec identity is separate from decoder/encoder availability. |
| Hardware discovery and pairing | `CameraDeviceSystem`, `CameraPairing`, `NativeCameraSystem::devices/pair` | Validated discovery snapshots, exact-ID session pairing, ready/disconnected/unpaired status, revocable controls and explicit/RAII release. Apple pairing attaches actual camera input to a stopped session; hardware authentication requires a separate provider. |

`NativeCameraSystem` selects AVFoundation on Apple platforms, V4L2 on Linux,
Media Foundation on Windows and Camera2 NDK on Android API 24+. This covers
OS-visible built-in, USB/UVC, capture-card, external and virtual cameras where
the platform driver exposes them. Android reports every static metadata tag;
its live request/result controls remain owned by the host capture session.
See [compatibility and complete inspection](docs/Features.md).
Vendor providers use the same callback APIs without leaking native types.
FFmpeg supplies device/network capture independently, including RTSP/HTTP
sources and `avfoundation`, `dshow`, `v4l2`, and `lavfi` when enabled in its build.
Network URLs are explicit `MediaSource` inputs; PTP-only cameras and proprietary
authentication require a corresponding provider.

The new callback fields change public object layouts. Consumers must rebuild
against 0.4; CMake same-minor compatibility prevents linking 0.3 consumers to
this ABI. Existing aggregate callback source usage remains valid because the
new fields are optional and appended.

No API promises access to metadata that firmware or the OS hides. Unknown,
proprietary, future, and disabled codecs are not universally recognizable.
The catalog describes the connected build, including recognized identities without
an available encoder/decoder. Recording preserves the original codec; conversion
to a different codec is outside the current recording API.

## Build and verify

Requires CMake 3.24+, a C++23 compiler supporting `std::expected`, and (by default)
FFmpeg development libraries discoverable through pkg-config: `libavformat`,
`libavcodec`, `libavdevice`, `libavutil`, and `libswscale`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
```

For the repeatable shared/static/core package-consumer verification, run
`tools/verify.sh`. Use `tools/verify.sh --sanitize` to also run an ASan/UBSan
configuration with a compatible Clang compiler.
Use `tools/verify.sh --photo --sanitize` to build the sibling `iiGraphicAdjust`
source and verify the optional photo component, both SDKs and their installed
consumers. Every generated file and staging prefix stays under `build/`.

For a standard-library-only build, use
`-DIICAMERA_WITH_FFMPEG=OFF -DIICAMERA_WITH_NATIVE=OFF`; metadata, validation,
optics, and custom control ports remain usable. Media/native functions explicitly
return unavailable. Shared and static builds are supported. All build variants,
fixtures, and package consumer builds must remain under `build/`.

The default installation prefix is `~/.local/SDK/iiCameraIntergration`. The
verification workflow stages the package inside `build/install` so all task
outputs remain in the Workspace. No third-party dependency is downloaded.

## Consume

```cmake
find_package(iiCameraIntergration 0.4 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiCameraIntergration::iiCameraIntergration)
```

```cpp
#include <iiCameraIntergration/iiCameraIntergration.h>
using namespace iiCameraIntergration;

auto cameras = NativeCameraSystem{}.cameras();
auto optics = rectilinearFieldOfView(36.0, 24.0, 50.0);
auto info = probeMedia(MediaSource{"footage.mov"});
auto frame = decodeRgbFrame(MediaSource{"footage.mov"});

auto camera = PacketCapture::open(MediaSource{
    .location = "0:none", .inputFormat = "avfoundation"
});
if (camera) {
    auto packet = camera->read(); // payload, stream index, PTS/DTS and time base
}
```

Device addresses, permissions, and supported pixel formats are provider-specific.
The host application owns privacy prompts and must supply its platform camera
usage descriptions. Enumeration does not request permission or start a session.
See [Native adapter](docs/Native.md) and [Media API](docs/Media.md) for platform,
threading, and cancellation details.

## Hardware discovery and pairing

```cpp
auto devices = NativeCameraSystem{}.devices();
auto found = devices.discover();
if (found && !found->empty()) {
    auto paired = devices.pair(found->front().id);
    if (paired) {
        auto current = paired->status();
        auto controls = paired->controls();
        auto released = paired->unpair();
        // Check every result. Otherwise destruction attempts resource release.
    }
}
```

Native session pairing requires existing camera authorization and does not start
capture. Device authentication/bonding is a distinct requested method; AVFoundation
returns `Unsupported` for it. The move-only owner revokes issued control ports
when released, and physical disconnection requires a new pairing. Custom hardware
providers use `DeviceCallbacks`. See [Devices](docs/Devices.md) and the current
[device verification](docs/DeviceVerification.md).

## Photograph processing component

An optional `iiCameraIntergration::ImageProcessing` target links to iiGraphicAdjust
0.5. It imports RGB24 with a verified color interpretation, applies EXIF orientation,
integer crop, existing development stages, existing resamplers, and histograms.
Unknown color tags require an explicit source interpretation. Known conflicting
transfer/primary tags, ICC input, wide-gamut and HDR transfers are rejected.
The FFmpeg decoder now applies supported YUV matrices and full/limited ranges;
it retains transfer/primary tags rather than silently interpreting every frame as sRGB.

```sh
cmake -S . -B build -DIICAMERA_WITH_IMAGE_PROCESSING=ON \
  -DiiGraphicAdjust_DIR=/path/to/iiGraphicAdjust/lib/cmake/iiGraphicAdjust
cmake --build build --parallel 3
```

```cmake
find_package(iiCameraIntergration 0.4 CONFIG REQUIRED COMPONENTS ImageProcessing)
target_link_libraries(my_app PRIVATE iiCameraIntergration::ImageProcessing)
```

For a synthetic photo processing example with an explicit sRGB interpretation:

```sh
build/iicamera-process lavfi 'color=c=0x404040:size=16x16:rate=10:duration=0.2' \
  build/developed.ppm srgb 1
```

The output is a real processed PPM frame; it is not evidence of physical photography.
`iiGraphicAdjust::developRaw()` supplies the separate caller-decoded Bayer sensor
path with linear-light headroom. See [Image processing](docs/ImageProcessing.md)
and [photo prerequisite audit](docs/PhotoAudit.md). RAW container decoding,
physical RAW capture, ICC conversion and general still-image encoding remain outside
this component. The example writes PPM; it does not add a photo encoder to the SDK.

## Inspection tool

```sh
build/iicamera-inspect --catalog
build/iicamera-inspect --cameras
build/iicamera-inspect --probe build/fixtures/capture.mkv
build/iicamera-inspect --capture avfoundation '0:none' build/camera.nut nut
```

For an entirely synthetic still capture, use `--frame lavfi
"testsrc=size=64x48:rate=10:duration=0.2" build/test-pattern.ppm`. This decodes
a real generated frame but provides no physical-camera evidence. `--capture`
requires streams that the destination container can remux; pointer-based native
frame packets such as FFmpeg `wrapped_avframe` require decoding first.

## Distribution

FFmpeg is an external dependency; its enabled libraries and license are returned
by `mediaBackendInfo()`. The current host dependency configuration is recorded in
[Verification](docs/Verification.md). A consumer's distribution must account for
the linked dependency configuration. Consult the primary [FFmpeg license
information](https://ffmpeg.org/legal.html). This package does not bundle FFmpeg.

See [Architecture](docs/Architecture.md), [Media API](docs/Media.md),
[Native adapter](docs/Native.md), and [Verification](docs/Verification.md).
