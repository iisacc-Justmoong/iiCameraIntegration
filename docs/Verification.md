# Verification

## 이미지 SDK 경계 이전, 2026-10-28

macOS arm64에서 파일 제공자와 이미지 SDK 검색을 비활성화한 카메라 SDK 빌드가
성공했고, 핵심 계약과 독립성 경계 검사 8개가 기존 제한 시간 안에 통과했다.
FFmpeg와 네이티브 장치 구현을 끈 별도 구성의 경계 및 사용 불가 검사 3개도
통과했다. 설치된 SDK는 두 SDK와 Qt 검색을 끈 소비자에서 컴파일됐으며,
바이너리 연결 목록에도 두 SDK가 없었다.

별도 사진 처리 소비자는 합성 RGB·색상·처리·입력 검증·FFmpeg 합성 소스
검사 5개를 기존 30초 제한 안에 통과했다. 별도 Media 소비자의 합성 미디어
검사는 녹화·재생용 데이터 확인, 제한·취소, 파일 생성·게시 오류, 기존 파일
보존과 게시 경합을 통과했다. 실제 오디오 재생은 하지 않았다. 테스트 파일은
파일 제공자가 `example/Media/resources`의 임시 디렉터리에 만들고 제거했다.

FFmpeg가 없는 구성에서도 사진 처리 합성 검사 5개가 통과했고, FFmpeg 전용
CLI·패키지 검사는 등록되지 않았다. 이 구성의 캡처 검사는 사용 불가 경로를
확인하므로 실제 영상 캡처 성공을 의미하지 않는다. 실제 카메라, 권한 요청과
애플리케이션은 실행하지 않았다. 두 CLI와 설치 패키지 실행 검사는 등록했지만
이번에는 실행하지 않았다. 격리된 설치본에서 임시 파일 제공자 제어 파일과
Finder 메타데이터의 제외를 검사한다. Windows·Linux 실제 빌드와 실행은 이번에
확인하지 않았다.

## Current 0.4.0 compatibility and bulk inspection

The 2026-10-13 results, native platform build matrix, 114 passing regression
executions, staged/local consumers and physical-device boundaries are recorded
in [FeatureVerification](FeatureVerification.md). The public batch API and
compatibility contracts are in [Features](Features.md).

## Historical 0.3.0 device discovery and pairing

The preceding device API and camera regression evidence are recorded in
[DeviceVerification](DeviceVerification.md). That version added seven
device cases while retaining the preceding media/processing tests. Physical
camera and OS authentication evidence remains explicitly separate.

## Historical 0.2.0 photo processing result

Camera 0.2.0 and iiGraphicAdjust 0.5.0 were built and executed on macOS arm64,
AppleClang 21.0.0. All new output and staging prefixes remain inside Workspace.
No running product was restarted, and the existing `~/.local` SDK installations
were not replaced.

| Configuration | Camera CTest | iiGraphicAdjust CTest | Installed consumers |
| --- | ---: | ---: | --- |
| Release shared, optional processing enabled | 21/21 | 150/150 | 1/1 each |
| Release static, optional processing enabled | 21/21 | 150/150 | 1/1 each |
| Debug shared, ASan + UBSan | 21/21 | 150/150 | 1/1 each |
| Release static, independent camera core | 9/9 | No dependency | 1/1 |

The added color suite checks 26 analytic color cases. Five bridge cases check
RGB24 stride/padding, source retention, processing order, transfer conversion,
tag conflicts, malformed input, subquantization alpha rejection and generated
capture. CLI tests check actual +1 EV gray pixels and existing-file preservation.
The package test installs the built artifacts, executes the installed CLI,
accepts core and optional requests without the image dependency, and rejects a
required request when that dependency is unavailable. These regressions caught
and fixed an external dependency runtime-search-path issue and incorrect
`OPTIONAL_COMPONENTS` handling. Installed-consumer linkage alone would not have
caught the CLI loader failure.

iiGraphicAdjust adds six independently checked RAW cases and four photographic
helper cases; its 140 existing rendered-image suites are retained. Tests verify
all four Bayer patterns/phase offsets, measured-site and bilinear arithmetic,
calibration matrices/black levels/gains, unclipped linear headroom, all eight EXIF
orientations, exact crop and alpha-weighted RGB/linear-luminance statistics.
The original test-first missing-implementation failures and the gray-range
regression are separate from passing final evidence.

`DYLD_PRINT_LIBRARIES` on the shared consumer confirms the staged camera 0.2.0,
ImageProcessing 0.2.0 and iiGraphicAdjust 0.5.0 dylibs. The installed CLI is
also executed directly; shared/static behavior and package dependency tests
therefore do not rely on a previously installed SDK. ASan/UBSan instrument both
SDKs, while FFmpeg/system libraries remain external. LeakSanitizer is unavailable
on this macOS toolchain (`ASAN_OPTIONS=detect_leaks=0`); no leak-proof claim is made.

Run `tools/verify.sh --photo --sanitize` for the repeatable complete workflow;
`--help` lists the flags. Without `--photo`, the optional component is explicitly
disabled for every variant. The image SDK is resolved from its sibling source,
not downloaded. Each variant builds into `build/` and stages independently.

Evidence includes `build/photo-workflow.log`, `build/photo-verification-report.json`,
the per-variant JUnit files, `build/photo-consumer/runtime.log`, and installed CLI
artifacts under `build/processing-package/`. The public contracts are documented
in [ImageProcessing](ImageProcessing.md), [PhotoAudit](PhotoAudit.md), and the
iiGraphicAdjust RAW/photo-operation documents.

This is numerical/host package proof. The connected host still has no physical
camera. Native high-quality still capture, physical optical control, encoded RAW
file decoding, arbitrary ICC/wide-gamut conversion, profile calibration, EXIF/XMP
serialization, high-bit-depth photo encoding and device-platform runtime remain
outside the implemented/verified photo scope. The RGB24 bridge is an SDR
development pipeline; the separate unpacked Bayer API preserves linear headroom
until rendering. Neither establishes that all photographic features are complete.

## Historical 0.1.0 camera result

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
`build/install/lib/libiiCameraIntegration.0.1.0.dylib`.

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
The default `~/.local/SDK/iiCameraIntegration` prefix has not been modified.
