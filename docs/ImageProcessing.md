# Captured photograph processing

독립적으로 구성하는 `example/ImageProcessing` 소비자가 카메라 코어와
`iiGraphicAdjust` 0.6.x를 조합한다. 두 SDK는 서로를 검색하거나 링크하지 않는다.
카메라 코어는 어댑터나 iiGraphicAdjust 없이 사용할 수 있다.

`processPhoto(RgbFrame, PhotoProcessingOptions)` returns a detached normalized
sRGB `Image`, an `ImageHistogram`, original frame metadata as provenance, source
PTS/time base, and processing metadata. Stages run in this order:

1. Validate RGB24 dimensions/stride/buffer and source color interpretation.
2. Convert sRGB, Rec.709, or linear-sRGB code values to the existing sRGB pixel contract.
3. Apply one of the eight explicit EXIF orientations.
4. Apply an optional integer crop in the oriented image's coordinates.
5. Run the existing `iiGraphicAdjust::develop` SDR stages.
6. Apply optional Box/Bilinear/Mitchell/Lanczos3 resizing.
7. Calculate encoded RGB and linear-luminance histograms/statistics.

The default `FrameEncoding::Metadata` requires a supported transfer tag and
Rec.709 primaries. Explicit `Srgb`, `Rec709`, or `LinearSrgb` interpretation fills
missing information only. It cannot override known conflicting tags. Unknown
source YUV matrix/range assumptions remain observable in frame provenance; they
are not physical calibration proof. ICC-bearing, wide-gamut, unsupported-transfer,
PQ and HLG frames need caller color normalization and are rejected here.
The decoder's 8-bit RGB conversion cannot retain arbitrary RAW/HDR precision.

`captureAndProcessPhoto` composes first-frame decode with `processPhoto`.
It does not invoke a native high-quality photo output session, discover EXIF
orientation automatically, capture Bayer RAW, or return camera-profile calibration.
The caller supplies orientation explicitly. Cancellation/timeouts bound decoding;
CPU processing afterward is synchronous and has no separate hard time limit.

`photoFrame` exports opaque sRGB results as packed RGB24. Any alpha below one is
rejected **before quantization**, including .999, so conversion cannot silently
remove partial transparency. Source metadata is copied with the
`iiCamera.photo.source.` prefix. Original orientation/dimensions therefore remain
provenance and cannot masquerade as output encoding tags. Output processing
orientation is always 1 after the selected operation. This SDK does not serialize
these dictionaries into EXIF/ICC/XMP bytes.

The `iicamera-process` example writes a PPM image exclusively, preserves an
existing file, and demonstrates real capture/decode/development pixels. It assumes
only the explicit color interpretation given in its command arguments.

설치된 카메라와 iiGraphicAdjust 패키지를 사용해 `example/ImageProcessing`을
별도로 구성한다. 설치 패키지명은 `iiCameraImageProcessing`이며, 이를 찾은 뒤 기존
`iiCameraIntegration::ImageProcessing` 타깃을 링크한다. 이전 루트 옵션
`IICAMERA_WITH_IMAGE_PROCESSING=ON`은 이전 안내와 함께 실패한다. 카메라 패키지에
`ImageProcessing`을 필수로 요청하면 실패하고 선택 요청은 사용할 수 없는 상태다.
기존 호출자는 새 패키지를 사용하도록 변경하고 다시 빌드해야 한다.

FFmpeg·도구·테스트를 켜면 `iiCamera.processing.package`는 어댑터를 build에 설치하고
의존성이 없는 경우의 동작을 확인한 뒤 선택한 iiGraphicAdjust 패키지로 독립 C++23
소비자를 빌드·실행한다. 소비자는 처리된 RGB 픽셀·히스토그램·RAW 현상을 검사한다.
이때 카메라 SDK는 iiGraphicAdjust를 검색하지 않는다.

카메라 패키지가 공개하는 `iiCameraIntegration_WITH_FFMPEG` 값으로 CLI 및 패키지
실행 검사 등록 여부를 판단한다. FFmpeg가 없는 구성에서도 합성 이미지 처리 검사는
유지하며 FFmpeg가 필요한 CLI 실행 검사는 등록하지 않는다.

The installed processing library/tool use their own library directory as a runtime
search path and include external linked-library directories. When iiGraphicAdjust
is staged in a separate prefix, that prefix must remain available at runtime. A
common prefix permits the relative library path to resolve both SDKs. Deployment
must supply the appropriate FFmpeg and iiGraphicAdjust binaries as well as camera
binaries; this SDK does not bundle or license those dependencies automatically.

`tools/verify.sh --photo --sanitize` verifies both SDKs, shared/static/sanitized
installed consumers, the independent core, the installed processing CLI, and the
optional/required dependency behavior. [Verification](Verification.md) records
the actual host evidence and the remaining photographic features.

The encoding formulas follow the familiar piecewise sRGB transfer and Rec.709
inverse transfer. Primary references are [W3C color conversion documentation](https://www.w3.org/TR/css-color-4/#color-conversion-code)
and [ITU-R BT.709](https://www.itu.int/rec/R-REC-BT.709).
