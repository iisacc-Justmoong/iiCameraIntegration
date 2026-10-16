# Device compatibility and bulk feature inspection

All adapters and the public core use C++23 without Qt. A feature report describes
what a connected camera, its driver and its platform API expose. It cannot promise
access to hidden firmware functions or universal compatibility with every model.

## Native compatibility

| Platform | Adapter | Reported capabilities | Session and controls |
| --- | --- | --- | --- |
| macOS | AVFoundation | OS-visible built-in, external/USB, virtual, Continuity and Desk View cameras; formats/rates, color spaces, autofocus systems, photo dimensions, flash/torch and focus/exposure-point support | Input attached to a stopped session; supported focus/exposure/white-balance modes and torch |
| iOS / Mac Catalyst / tvOS | AVFoundation | Wide/ultrawide/telephoto, TrueDepth, dual/triple, LiDAR and external devices where exposed; mobile depth, HDR, stabilization and constituent metadata | Device-supported zoom, lens position, aperture, ISO, exposure duration/compensation; capture not started |
| Linux | V4L2 | Capture and multi-planar nodes: USB/UVC, platform/CSI, capture cards, loopback; formats/sizes/rates, driver/bus identity, active format and standard/private/compound controls | Open descriptor without streaming; current ranges/steps/flags and standard control read/write mappings |
| Windows desktop | Media Foundation | Symbolic links, all exposed video media types and presentation/stream/media-type attributes; all seven IAMCameraControl and ten IAMVideoProcAmp properties where supported | Unstarted media source; owned COM MTA worker accepts calls from STA and other threads |
| Android API 24+ | Camera2 NDK | IDs, lens/sensor characteristics, output formats and every static tag returned by the NDK, including vendor tags | Open device without a capture session; live request/result controls belong to the host capture session |
| Other / native disabled | Explicit fallback | Unavailable | Unavailable |

An installed backend is not evidence that a particular physical device or OS was
tested. Discovery never requests permission. Linux query opens a descriptor;
Windows feature queries activate an unstarted source; Apple/Android metadata
queries do not create capture sessions. No feature query starts streaming or
changes settings. Linux/Windows/Android global permission status is NotDetermined;
actual device operations retain PermissionDenied or Busy errors.

FFmpeg supports explicit device/network MediaSource inputs, including RTSP/HTTP
when its build provides those protocols. Network URLs are not automatically
included in native discovery. DSLR/mirrorless devices in webcam/UVC mode use
their OS drivers. PTP-only cameras, ONVIF configuration and proprietary vendor
authentication require a corresponding callback provider.

## Public API

```cpp
#include <iiCameraIntergration/iiCameraIntergration.h>
using namespace iiCameraIntergration;
NativeCameraSystem system;
auto all = system.features();
if (all) {
    for (const auto& item : *all) {
        if (!item.features) continue; // Per-camera failure; other entries survive.
        const auto& report = *item.features;
        for (const auto& reading : report.controls) {
            if (!reading.value) continue; // Per-control Error is preserved.
            // reading.capability and *reading.value describe this control.
        }
        // report.camera preserves all formats, lenses, sensor and metadata.
        // report.nativeFeatures preserves native/vendor IDs, types and values.
    }
}
auto metadata = system.features(FeatureQueryOptions{false});
// auto one = system.features(exactDeviceId);
// auto paired = pairing.features();
```

ControlPort::queryAll visits every advertised capability in order. Read-only
controls remain queryable; write-only controls yield Unsupported without a query
callback. Exceptions and malformed returned values become per-item BackendFailure.
A control absent from capabilities is not exposed by this provider. Snapshots are
not frame-synchronized atomic measurements; refresh after format changes/unplug.

CameraFeatures contains complete CameraInfo, ControlReading entries and
NativeFeature entries. NativeFeature retains backend ID/name, FeatureType,
readable/writable flags, optional range, menu choices, optional value/error and
namespaced metadata. FeatureValue supports int64, double, bool, string, byte
payloads and integer/number arrays. Native writable means backend support;
ControlPort exposes standard mutations, not arbitrary vendor-payload writes.

With includeCurrentValues=false, standard readings are omitted and native current
control values remain unset. Static Camera2 characteristics, AVFoundation flags
and MF attributes stay present. NativeFeature::value distinguishes unqueried,
successful and failed values; no failure is replaced by a default value.

Outer batch errors represent discovery failure. After discovery each camera gets
its own Result<CameraFeatures>. A failed control port is retained per readable
control while metadata survives. Provider-level failures become camera error
entries. Callback batches discover once; adapters may re-resolve exact IDs to
check hot unplug.

## Provider and ownership contracts

DeviceCallbacks appends optional controls(id) and features(id, options) callbacks.
Providers can implement full native reports or use queryCameraFeatures to assemble
standard reports. Missing control providers yield per-item Unavailable; metadata
queries still work. Callback exceptions, wrong IDs, invalid metadata and duplicate
native IDs are rejected. New fields are optional and preserve aggregate source
usage; object layout changes require a 0.4 consumer rebuild.

PairingCallbacks appends features(options). The pairing holds its existing
revocable gate for the entire batch; unpair cannot release resources mid-query.
Released, moved or disconnected handles cannot invoke feature providers. Fallback
queries use the provider port directly under the gate without reentering a guarded
wrapper. Callbacks must not reenter their own pairing.

## Platform units and full native data

V4L2 exposure units convert from 100 microseconds to seconds; pan/tilt convert from
arcseconds to degrees. Unknown physical focus/zoom units remain native features.
Enumeration uses NEXT_CTRL | NEXT_COMPOUND, followed by QUERYCTRL and legacy
user/camera/private fallbacks. Disabled controls are skipped and menu holes remain
excluded. Integer64/string/compound values use G_EXT_CTRLS, with G_CTRL fallback
for compatible legacy scalars. Native integer64/payload values remain lossless.
Payloads are bounded at 16 MiB and control/menu enumeration at 65,536 entries;
limits/errors remain observable in metadata. Stepwise frame-interval steps are
retained as rational metadata; reciprocal FPS bounds do not promise uniform FPS
steps or all stream combinations.

Windows exposure converts log2 seconds to an explicit discrete seconds list.
Focus converts millimeters to meters; iris converts f-stop times ten to f-number;
documented camera zoom is focal length in millimeters. Commands refresh driver
capabilities before writing. Native properties retain raw values/defaults/flags.
GUID media subtypes are not guessed to be recording codecs or pixel formats.

Android rational tags retain numerator/denominator pairs. Aperture/focal-length
lists describe static support, not current values. Minimum frame duration remains
per-format metadata without inferred FPS/request/stream combinations. Capture
request/result controls and vendor tag names are not invented.

## CLI

```sh
iicamera-inspect --features
iicamera-inspect --features DEVICE_ID
iicamera-inspect --features --metadata-only
```

Output is JSON with schemaVersion: 1 and devices. Device, standard control and
native feature errors remain at their respective levels. Camera objects include
every public field and nested metadata map. A failed device returns exit 1 after
emitting the batch; per-control errors remain within a successful report. Invalid
options/extra IDs are rejected. Public batch APIs remain in core-only builds.
Use find_package(iiCameraIntergration 0.4 CONFIG REQUIRED) and rebuild consumers.

Primary contracts: [V4L2 enumeration](https://cdn.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html),
[V4L2 camera units](https://cdn.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-camera.html),
[Media Foundation discovery](https://learn.microsoft.com/en-us/windows/win32/medfound/enumerating-video-capture-devices),
[Windows control units](https://learn.microsoft.com/en-us/windows/win32/api/strmif/ne-strmif-cameracontrolproperty),
[Microsoft MF controls](https://github.com/microsoft/psi/blob/master/Sources/Media/Microsoft.Psi.Media_Interop.Windows.x64/MediaCaptureDevice.cpp),
[Camera2 NDK](https://developer.android.com/ndk/reference/group/camera) and
[AVFoundation](https://developer.apple.com/documentation/avfoundation/avcapturedevice).
