# Captured photograph processing

The optional `ImageProcessing` component depends publicly on camera core and
`iiGraphicAdjust` 0.5. The standard C++ processing library has no reverse dependency.
Camera core remains usable without this component or iiGraphicAdjust.

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

Build with `-DIICAMERA_WITH_IMAGE_PROCESSING=ON` and a compatible iiGraphicAdjust
0.5 package. Consume with `find_package(iiCameraIntergration 0.2 CONFIG REQUIRED
COMPONENTS ImageProcessing)` and link `iiCameraIntergration::ImageProcessing`.
A package built without the component correctly rejects a required component request.
Core-only requests do not search for iiGraphicAdjust. With `OPTIONAL_COMPONENTS
ImageProcessing`, an unavailable iiGraphicAdjust leaves core usable and sets the
component's found flag to false. A required ImageProcessing request fails instead.

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
