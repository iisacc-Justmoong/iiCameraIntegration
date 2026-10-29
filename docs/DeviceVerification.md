# Hardware discovery and pairing verification — 0.3.0

On 2026-10-09 (Asia/Seoul), iiCameraIntegration 0.3.0 was built and executed on
macOS arm64 with AppleClang 21.0.0. This change adds only camera discovery and
pairing APIs; it does not add capture, image processing, a GUI, or a device
authentication protocol. The existing iiGraphicAdjust 0.5.0 dependency is reused
for camera regression tests and its source is unchanged.

| Configuration | Camera CTest | Installed public-package consumer |
| --- | ---: | ---: |
| Release shared, FFmpeg/native/optional processing enabled | 28/28 | 1/1 |
| Release static, same components | 28/28 | 1/1 |
| Debug shared, ASan + UBSan, same components | 28/28 | 1/1 |
| Release static, independent standard C++ core | 16/16 | 1/1 |

The seven new device cases cover discovery snapshots, exact-ID/method preflight,
provider dispatch order, ready-state validation, move-only ownership, repeated
release, failed-release retry, destructor cleanup, weak revocable control ports,
latched physical disconnection, invalid UTF-8 and malformed provider metadata.
An eight-thread query/apply fixture checks serialization. A separately paused
query races against unpair to check that resource release occurs after the query
exits, and subsequent escaped-port operations are blocked. Provider errors and
exceptions remain distinct from invalid caller arguments.

The test-first link attempt against the preceding implementation failed with
missing device/pairing definitions; `build/devices-tdd-red.log` records it.
Final passing results are separate. Every device-success fixture is artificial
and is identified as such; it does not prove native input acquisition or bonding.

The native smoke test enumerates the host and rejects invalid/unknown IDs,
unsupported DeviceBond requests and native provider parameters without prompting
for camera permission or starting capture. The host reports **zero cameras** and
unchanged NotDetermined permission. Native input/session attachment success,
physical hot-unplug, release and optical-control runtime remain unverified.

The changed native adapter was additionally compiled as an object with C++23,
ARC and warnings treated as errors for macOS 14 arm64, iOS 16 arm64 against the
installed iOS 27 SDK, and Mac Catalyst 16 arm64. These object checks do not prove
an iOS/Catalyst package link or device execution. Windows/Linux/Android native
discovery/pairing remain unavailable; custom providers can use the portable API.
AVFoundation pairing prepares a stopped camera session and explicitly rejects
OS/device bonding. Bluetooth/Wi-Fi authentication is not implemented here.

Installed consumers use only public headers and imported CMake targets, exercise
all new APIs with a fixture and perform read-only native discovery. Loader
inspection confirms the staged camera and processing 0.3.0 libraries. Both new
public headers and updated documentation are compared with the staged package.
ASan/UBSan instrument the SDK; external libraries are not rebuilt. LeakSanitizer
is unavailable on this macOS toolchain.

All build/consumer/staging output remains under `build/`. Existing `~/.local`
installations and running products were not replaced or restarted. Source/build
proof is recorded in `build/devices-verification-report.json`, JUnit files
`build/devices-{shared,static,sanitized,core}-ctest.xml`, test logs with the same
variant names, and `build/device-native-{macos,ios,catalyst}.log`.

`tools/verify.sh --photo --sanitize` reproduces the complete SDK workflow,
including unchanged image-processing suites. For the device-only API tests, use
`ctest --test-dir build -R 'iiCamera.devices' --output-on-failure` after building.
See [Devices](Devices.md) for contracts and examples and
[historical verification](Verification.md) for earlier component evidence.
