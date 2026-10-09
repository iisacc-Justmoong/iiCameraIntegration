# Verification — 2026-10-09 (Asia/Seoul)

The C++23 library was built and executed on macOS arm64 with AppleClang 21.0.0.
All task outputs and installation staging prefixes remain inside the Workspace.
The existing SDK aggregate repository had no changes before or after this task.
The new module is an independent local Git repository.

| Configuration | CTest | Staged package consumer |
| --- | ---: | ---: |
| Release shared, FFmpeg + Apple native | 13/13 | 1/1 |
| Release static, FFmpeg + Apple native | 13/13 | 1/1 |
| Release static, standard C++ core only | 9/9 | 1/1 |
| Debug shared, ASan + UBSan, FFmpeg + Apple native | 13/13 | Not separately installed |

The test suite covers metadata extension retention, malformed and inconsistent
metadata, numeric range/step/discrete validation, capability-aware control
preflight and delegation, provider exceptions/lifetime, independent optical
reference values, native discovery without permission changes, packet EOF/cancel,
actual RGB pixel patterns, remux reopen/redecode pixel identity, existing-file
preservation, recording limits and partial-file cleanup, and CLI frame artifacts.
Sanitizers instrument this SDK; external system and FFmpeg libraries are not
rebuilt with sanitizer instrumentation.

The format suite checks PNG, JPEG, WAV, FLAC, MP4, WebM, and Matroska via file,
caller-owned bytes, and a misleading `.txt` extension (21 recognition cases).
The fixtures contain png, mjpeg, pcm_s16le, flac, h264, aac, vp9, opus, and ffv1.
A signature-only PNG is recognized with incomplete parameters, independently of
full media validity. Random input and empty byte input return distinct errors.
These are representative content tests, not an exhaustive test of every codec.

The installed shared CLI identified MP4/H.264/AAC from a generated artifact.
`DYLD_PRINT_LIBRARIES` confirmed that the consumer loads the staged
`build/install/lib/libiiCameraIntergration.0.1.0.dylib`.

## Connected FFmpeg inventory

| Inventory | Observed count |
| --- | ---: |
| Registered codec identities | 539 |
| Identities with at least one decoder implementation | 498 |
| Identities with at least one encoder implementation | 175 |
| Registered format entries | 414 |
| Input demuxer entries | 361 |
| Output muxer entries | 184 |

Actual runtime library versions are `libavformat/libavcodec/libavdevice 63.1.102`,
`libavutil 61.1.102`, and `libswscale 10.1.102`. All five linked Homebrew libraries
report **GPL version 3 or later**. This development dependency configuration is
not a distribution license decision. The standard-library core has no FFmpeg
runtime dependency, and another FFmpeg build can expose a different inventory
and license. See the primary [FFmpeg licensing information](https://ffmpeg.org/legal.html).

## Native and target-runtime evidence

Final native adapter C++23/ARC checks with warnings treated as errors passed for
macOS arm64, iOS 16 arm64 using the installed iOS 27 SDK, and Mac Catalyst 16
arm64. iOS/Mac Catalyst evidence is a compile check of the native adapter, not a
full target package build or physical device execution. The variable-aperture
branch is SDK/OS availability guarded.

Host AVFoundation enumeration returned **0 cameras and 0 formats**;
`system_profiler SPCameraDataType` also returned an empty camera list.
Authorization remained NotDetermined. No camera capture session was started.
Physical camera discovery metadata, hardware controls, physical media capture,
iOS/Mac Catalyst runtime, and Windows/Linux/Android runtime are unverified.
Windows/Linux/Android native optical control adapters are not implemented; the
FFmpeg input formats and custom C++ control port remain the integration boundary.

## Reproduce and inspect evidence

Run `tools/verify.sh --sanitize` with the installed FFmpeg development libraries
and a compatible Clang toolchain. Shared/static/core builds and consumer prefixes
are created beneath `build/`. To skip the sanitizer configuration, omit the flag.

- `build/verification.log`: complete shared/static/core/sanitizer workflow output.
- `build/verification-report.json`: the recorded matrix and evidence boundaries.
- `build/ctest-results.xml`, variant `ctest-results.xml`: JUnit test results.
- `build/consumer/runtime.log`: installed shared-library load evidence.
- `build/catalog-evidence.txt`: registry, versions, and runtime licenses.
- `build/native-evidence.txt`: installed native enumeration result.
- `build/installed-probe-evidence.txt`: installed MP4/H.264/AAC content probe.
- `build/native-compile-{macos,ios,catalyst}.log`: native adapter compile commands.
- `build/cli/cli-red.ppm`: a synthetic captured-frame artifact.

This verification installed only into Workspace staging directories.
The default `~/.local/SDK/iiCameraIntergration` prefix has not been modified.
