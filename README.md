# iiCameraIntergration

A C++23 camera integration SDK for camera metadata, capability-aware optical
controls, media capture, and codec/container identification. The package and
namespace preserve the requested spelling **iiCameraIntergration**.
The public API uses standard C++ values and `std::expected`; it has no Qt dependency.
Native platform code is isolated in an optional adapter.

| Requirement | API | Implemented behavior |
| --- | --- | --- |
| Camera metadata | `CameraInfo`, `SensorInfo`, `LensInfo`, `CameraFormat` | Device identity, sensor/lens fields, format/frame-rate capabilities, active format, controls, and provider extension metadata. Unknown measurements remain optional. |
| Lens, field of view, variable aperture | `ControlPort`, `ControlCapability`, `ControlCommand` | Typed read/write commands with supported units, ranges, steps, modes, and identifiers. Rejects unsupported/read-only controls before dispatch. Includes rectilinear optical conversion functions. |
| Camera media capture | `PacketCapture`, `decodeRgbFrame`, `recordMedia` | FFmpeg device/file/network input, timestamped stream packets, decoded RGB still frames, and bounded recordings by remuxing compatible streams. |
| Codec/format recognition | `codecCatalog`, `formatCatalog`, `probeMedia`, `probeMediaBytes` | All codec descriptors and input/output formats registered in the linked FFmpeg build. Content probing identifies container and each stream codec independently of its extension; codec identity is separate from decoder/encoder availability. |

`NativeCameraSystem` provides Apple AVFoundation metadata and controls. macOS
supports device/format enumeration and the focus/exposure modes exposed by the OS.
iOS supports additional optical controls when the device and OS provide them.
Other native control providers use `ControlPort` callbacks without depending on
Apple or FFmpeg types. FFmpeg supplies capture input formats such as `avfoundation`,
`dshow`, `v4l2`, and `lavfi` only when enabled in its build.

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
find_package(iiCameraIntergration 0.1 CONFIG REQUIRED)
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
