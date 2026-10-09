# Photograph and decoded-color audit

The original four camera-integration requirements are implemented as bounded
APIs, not as a claim that every photographic feature or device has been completed.
Metadata/control contracts, media probing, packet capture, RGB24 frame decoding,
and compatible-container remuxing have automated host evidence. Physical camera
controls/capture and target-platform runtime need separate hardware evidence.

## RGB24 color contract

`decodeRgbFrame()` resolves each source color property from the decoded `AVFrame`
first and falls back to the stream codec parameters when that frame property is
unspecified. For YUV and gray inputs, it configures the conversion matrix and
source range explicitly. Supported matrix families are BT.709, FCC, BT.470BG and
SMPTE 170M (BT.601 coefficients), SMPTE 240M, and BT.2020 nonconstant luminance.
Other YUV matrices return `Unsupported`; BT.2020 constant luminance, ICtCp,
chromaticity-derived matrices and YCgCo are not silently mapped to BT.601.
Sensor Bayer/XYZ formats and limited-range RGB also return `Unsupported` because
this helper has no corresponding color transform.
Gray input is first quantized as full-range code values and then, for declared
limited range, expanded explicitly from 16..235 to 0..255. This avoids swscale
gray fast paths that ignore a requested limited-range conversion. It remains an
eight-bit output operation, not preservation of a high-bit-depth gray original.

The public `RgbFrame` fields describe the **output** encoding:

| Field | Meaning |
| --- | --- |
| `colorSpace` | `gbr`: encoded RGB components, not an sRGB profile assertion |
| `colorRange` | `pc`: full-range RGB24 code values |
| `colorTransfer` | Resolved source transfer name, or `unknown`; unchanged |
| `colorPrimaries` | Resolved source primaries name, or `unknown`; unchanged |

Matrix/range conversion does not perform transfer-function, gamut, or ICC
conversion. A BT.709 transfer or a PQ transfer remains so tagged after decoding.
Do not feed such pixels into an sRGB-only image processor without a valid
normalization step or an explicitly documented caller interpretation.

When the source matrix is absent, YUV/gray uses BT.601 coefficients and RGB uses
its RGB identity. An unspecified source range defaults to limited for YUV,
except legacy `yuvj*` formats, and to full for RGB/palette/gray formats. These are
assumptions, not discovered color facts. Source transfer and primaries remain
`unknown` when absent. The frame metadata preserves the following provenance:

| Metadata key | Value |
| --- | --- |
| `iiCamera.color.sourceSpace` | Resolved or assumed source matrix name |
| `iiCamera.color.sourceRange` | Resolved or assumed source range name |
| `iiCamera.color.sourceSpaceAssumed` | `true` when neither frame nor codec parameters specified it |
| `iiCamera.color.sourceRangeAssumed` | `true` when neither frame nor codec parameters specified it |
| `iiCamera.color.transferConverted` | `false` |
| `iiCamera.color.primariesConverted` | `false` |
| `iiCamera.color.precision` | `8-bit` |
| `iiCamera.color.hdrPreserved` | `false` |
| `iiCamera.color.iccProfilePresent` | Presence in decoded-frame or coded-stream side data; profile bytes are not retained |

The output has three channels and no alpha. Eight-bit conversion may discard
input precision and extended radiance. Keeping an HDR transfer tag is not HDR
preservation. This helper does not apply EXIF/display-matrix orientation, preserve
RAW sensor samples, or provide a still-image encoder.

## Processing module boundary

`iiGraphicAdjust` already supplies CPU sRGB SDR development, tone/color/detail
adjustments, masks, compositing and four resize kernels. Its `cameraRawFilter`
operates on rendered RGB; it is not a RAW decoder or sensor ISP. Camera processing
integration should reuse those operations and keep camera types out of the
lower-level image-processing SDK. A separate optional bridge may depend on both
libraries without introducing a circular dependency.

## Automated color validation

`tests/color_decode.cpp` builds constant YUV444 frames with declared matrix/range
metadata. Expected pixels come from independent luma-coefficient equations with
two-code-value tolerance for fixed-point rounding. It checks all six supported
matrix names in full/limited range, neutral endpoints, observable raw-YUV color
assumptions, RGB transfer/primaries preservation, explicit PQ quantization limits,
and rejection of unsupported transforms. Generated raw fixture files remain
under the caller-supplied `build/` directory. No physical camera is used.

The conversion API and its range/coefficient parameters are described by the
primary [FFmpeg libswscale documentation](https://ffmpeg.org/doxygen/trunk/group__libsws.html).

On 2026-10-09 (Asia/Seoul), the updated library and color/media test executables
built on macOS arm64. `iiCamera.color_decode`, its 26 color cases, and the existing
`iiCamera.fixture`/`iiCamera.media` tests passed (3/3 CTest entries). The gray
limited-range regression was observed before the explicit expansion fix and
passed after it. This evidence uses synthetic/raw fixtures, not camera hardware.
