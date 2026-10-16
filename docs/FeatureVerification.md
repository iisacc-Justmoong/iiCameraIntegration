# Compatibility and bulk feature verification — 0.4.0

Verification date: 2026-10-13, Asia/Seoul. The source and public ABI version is
0.4.0. Runtime checks use macOS arm64, AppleClang 21 and the installed FFmpeg
63.1.102 family. The portable SDK and adapters have no Qt dependency.

| Host configuration | Passing CTest cases | Independent staged-package consumer |
| --- | ---: | ---: |
| Release shared, native + FFmpeg + existing optional photo component | 35/35 | 1/1, including photo component |
| Release static, native + FFmpeg | 28/28 | 1/1 |
| Release static, native/media-disabled core | 23/23 | 1/1 |
| Debug shared, native + FFmpeg, ASan + UBSan | 28/28 | 1/1 |
| Actual local installation, native + FFmpeg Core | Separately built and installed | 1/1 |

The four regression suites contain 114 test executions with no failures. Counts
include optional artificial V4L2 and Camera2 driver fixtures, compiled against
Linux UAPI and official Android headers. These fixtures do not execute hardware
or run Linux/Android operating systems. ASan/UBSan instrument this SDK; external
FFmpeg/system libraries are not rebuilt. LeakSanitizer is disabled on this host.

New contracts check complete control traversal, readable/write-only handling,
per-item permission/busy/failure results, malformed values, metadata-only reads,
all-camera results, native vendor payload preservation, invalid/duplicate/wrong
IDs, callback exceptions, pairing revocation and concurrent batch/unpair gating.
JSON tests parse the emitted structure and check quoted/UTF-8 names, byte arrays
and nested errors. V4L2 fixtures check modern/legacy control enumeration, int64 and
compound values, read-only flags, partial payload failure, step rejection and
unplug. Camera2 fixtures check sensor/lens/output metadata, all vendor tags,
permission failure, no metadata-triggered opening, disconnect and release.

| Target adapter | Compilation / artifact evidence | Hardware/runtime evidence |
| --- | --- | --- |
| macOS AVFoundation | Shared/static and instrumented execution on the host | Read-only discovery returns zero cameras; authorization stays NotDetermined |
| Linux x86_64 V4L2 | Full shared library and inspection-tool cross build | No Linux runtime or physical-camera test |
| Windows x86_64 Media Foundation | Full DLL/import library and inspection EXE cross build | No Windows runtime or physical-camera test |
| iOS arm64 AVFoundation | Full CMake static-library cross build, iOS 17 deployment with the installed iOS 27 SDK | No iPhone/iPad execution |
| Mac Catalyst arm64 AVFoundation | Native adapter compiles against the installed SDK with a Catalyst 17 target | No linked Catalyst product/runtime test |
| Android Camera2 | Source compiles against official Camera2/JNI/native-window headers; artificial NDK fixture executes on the host | No full Android NDK package link or physical-device execution |

Linux/Windows cross builds use the official Zig 0.15.2 distribution verified
against its published SHA-256. Zig's bundled libc++ enables the removed legacy
std::unexpected function; the cross toolchain first includes __config and undefines
that compatibility switch so C++23 std::unexpected<T> is unambiguous. This is a
build-tool configuration workaround, not a modified standard-library source.

Actual installation is at ~/.local/SDK/iiCameraIntergration and contains the
0.4.0 Core library, native adapter, FFmpeg bridge, public headers, CMake package,
inspection CLI and documentation. The optional photo component was regression-
tested and staged; it is not part of this Core installation. The independent
local-package consumer passes, and dyld tracing confirms it loads the local
libiiCameraIntergration.0.4.0.dylib. Public Features.h and Devices.h match source;
private detail headers are excluded. The installed --features command emits
schemaVersion 1 with an empty device list on this host.

Physical input opening, optical/image control success, stream capture, real
hot unplug and device-specific combinations remain unverified. Network/RTSP,
PTP-only cameras and manufacturer authentication are not established by these
compile/fixture results. Build success is not universal device compatibility.

Evidence stays under build/: compatibility-ctest.xml, compatibility/{static,
core,sanitized}/build/compatibility-ctest.xml, corresponding workflow logs,
linux/windows/ios build logs, catalyst-syntax.log, android-syntax.log,
installed-consumer.runtime.log, installed-host-features.json and
compatibility-verification-report.json. All build/consumer outputs remain within
the module's build directory; only the requested SDK installation uses ~/.local.

Use tools/verify-compatibility.sh [all|static|core|sanitized] to reproduce the Core
host workflow and staged consumers. Optional fixture headers are selected with
IICAMERA_ANDROID_TEST_INCLUDE_DIR and IICAMERA_V4L2_TEST_INCLUDE_DIR. They are not
SDK dependencies or installed headers. The original tools/verify.sh --photo
--sanitize remains the full sibling photo-component workflow. Cross toolchain
files and fetched validation headers are retained only under build/toolchains
and build/android-headers. See [Features](Features.md) for the API and boundaries.
