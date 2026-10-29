# Media recognition and capture

The optional FFmpeg adapter provides actual demuxing, byte-content recognition,
compressed packet capture, one decoded RGB still frame, and bounded recording by
remuxing. The public header contains no FFmpeg types and requires only C++23.

## Recognition and availability

`codecCatalog()` enumerates FFmpeg's codec **descriptors**, including identities
that this build cannot decode or encode. Each `CodecInfo` separately lists the
installed decoder and encoder implementations. An empty list means that the
identity is recognized but that operation is unavailable. `formatCatalog()`
enumerates the installed demuxers and muxers with separate availability flags.
Format names can contain comma-separated FFmpeg aliases. This is a catalog of the
linked backend, not a claim that every codec or format ever created is supported.

`probeMedia()` opens the source and inspects its streams. Its default file path
mode uses FFmpeg's content probing; extensions are only hints. `probeMediaBytes()`
uses a seekable custom byte input without any filename or extension hint. It
reads the supplied span synchronously and does not retain it. Unknown, malformed,
encrypted, incomplete, or unsupported input can fail with an explicit error.
Headerless raw streams and devices need `inputFormat` and appropriate options.
Unknown input options are returned in `MediaInfo::unusedInputOptions`, preserving
provider-specific keys rather than silently treating them as applied.

`MediaInfo` exposes the container, metadata dictionary, duration if present, and
stream identities, codec IDs, codec tags, time bases, video dimensions, pixel
formats, color range/space/transfer/primaries, sample aspect ratio, codec
initialization bytes, audio sample formats, rates and channel layouts. The
`codecExtraData` bytes allow an external decoder to reconstruct configuration
records that are not present in every compressed packet. Missing or unknown
native properties remain empty or absent. Private vendor metadata is retained
only when FFmpeg exposes it; this API does not manufacture missing metadata.

`StreamInfo::parametersComplete` checks that a codec identity is present, that a
video stream has positive dimensions, and that an audio stream has a positive
sample rate and channel count. `MediaInfo::streamParametersComplete` requires at
least one stream and complete parameters for every stream. These flags establish
parameter presence, not media integrity or decodability. A truncated image
signature can therefore have a recognized codec with incomplete parameters;
decode success must be established separately. Codec initialization blobs larger
than 16 MiB are rejected as invalid media.

## Packet capture

```cpp
#include <iiCameraIntegration/Media.h>

iiCameraIntegration::MediaSource source;
source.location = "/path/to/capture.mkv";
auto capture = iiCameraIntegration::PacketCapture::open(source);
if (!capture) {
    // Inspect capture.error().code/message/nativeCode.
    return;
}
for (;;) {
    auto packet = capture->read();
    if (!packet) {
        // EndOfStream is the normal termination of finite input.
        break;
    }
    // packet owns its compressed payload. Stream metadata is capture->metadata().
    // A timestamp in seconds is timestamp * packet->timeBase.value().
}
```

Sources can be files, URLs, or FFmpeg device input formats such as `avfoundation`,
`v4l2`, or `dshow` when the linked FFmpeg build and target OS supply them. The SDK
passes arbitrary backend options through as strings; the application chooses the
device address and options for its platform. `lavfi` is a synthetic test source
and proves generated media processing, not physical camera operation.

Packets preserve stream indices, optional PTS/DTS, duration, keyframe state and
the stream time base. A packet contains encoded media bytes, not an RGB image.
The public packet currently omits FFmpeg packet side-data. Stream codec
initialization bytes are a snapshot taken when opening the source; midstream
configuration changes and packet-specific HDR/other side-data are not transported
by this packet API. A decoder needing those features should use the native
backend or a dedicated media adapter.
FFmpeg's `wrapped_avframe` packet representation contains native pointers rather
than a portable media bitstream. `PacketCapture::read()` explicitly returns
`Unsupported` for such packets; it never copies dangling pointer-bearing packet
structures into the public payload. Use `decodeRgbFrame()` to obtain owned pixels
from these inputs. Newer FFmpeg builds can produce `wrapped_avframe` for lavfi
sources; a generated-source catalog entry does not imply remux compatibility.
`decodeRgbFrame()` selects the first available video stream by default, decodes
its first frame, and converts it to tightly packed RGB24. `DecodeOptions` can
select a stream and bound pixel count, packet count and total elapsed time.
Hardware decoding, exposure correction, rotation application and color-managed
HDR display are outside this helper's contract. RGB conversion does not promise
bit-identical output across different FFmpeg versions or pixel-conversion builds.
The returned `RgbFrame` is full-range RGB24 (`gbr`, `pc`), with source transfer and
primaries retained. YUV matrices/ranges are resolved from the decoded frame, then
codec parameters; absent matrix/range information uses documented assumptions.
Unsupported transforms are rejected. ICC presence is reported without retaining
or applying the profile. Eight-bit conversion does not preserve HDR precision.
See [the decoded-color contract and numeric tests](PhotoAudit.md) and the optional
[photo processing component](ImageProcessing.md) before interpreting pixels as
sRGB.

The move-only `PacketCapture` session owns its input. Serialize `read()`,
`metadata()`, destruction and move operations on the caller side. `cancel()` can
be called concurrently with `read()` while the object's lifetime remains valid.
Cancellation is permanent. Reading a moved-from session returns `InvalidArgument`.

## Recording

`recordMedia(source, destination, files, options)` remuxes compatible video, audio and
subtitle streams into a local single-file container. It copies compressed media
without encoding or changing the codec. Data/attachment streams are skipped.
An incompatible container is an error; no transcoding is silently attempted.
Multi-file/segmented muxers are rejected because they cannot meet this function's
exclusive single-file output contract. Unknown output options appear in the
returned `unusedOutputOptions` dictionary.

`RecordingFilePort`는 호출자가 제공하는 세 연산이다. `createExclusive`는 기존
대상을 덮어쓰지 않는 탐색 가능한 바이너리 스트림을 반환하고, 생성에 실패하거나
예외가 발생하면 스스로 확보한 자원을 정리해야 한다. 성공한 스트림의 소유권은
동기 호출 동안 카메라 SDK에 이전되며 SDK가 닫는다. SDK는 성공 시 스트림을 닫은
뒤 `publishExclusive`로 덮어쓰기 없는 게시를 요청하고, 실패 시에는
`removeIncomplete`로 미완성 출력을 정리하도록 요청한다. 생성·게시 예외는 I/O
오류로 변환하고 정리 콜백의 예외는 호출 밖으로 전달하지 않는다.
`example/Media`는 iiFileProvider의 임시 파일을 사용하고 대상이 이미 있거나 게시
중 다른 파일이 생기면 덮어쓰지 않는다. POSIX에서는 하드 링크가 가능한 파일
시스템이어야 한다. SDK 자체는 파일 제공자나 다른 SDK를 검색·링크하지 않는다.
기존 경로만 받는 `recordMedia` 호출자는 포트를 제공하도록 수정하고 다시 빌드한다.

호출자는 출력 크기, 압축 페이로드 바이트, 패킷 수와 종료 이유를 받는다.
Cancellation, decode/mux/IO failure, or an empty recording requests cleanup
through the supplied port. A successfully finalized bounded recording is returned when its
packet, payload or duration limit is reached. Payload limits do not include
container overhead, and packet-count limits count only streams written.

Duration is bounded by both source timestamps and elapsed capture time, so fast
file remuxing and real-time device inputs are both bounded. Timestamp absence,
reordering, nonzero starts and discontinuities remain properties of the source.
Packet timestamps are rescaled to the output time bases, and the selected muxer
can apply its own timestamp shifting policy. A limit can end a GOP before its natural end,
so downstream display behavior still depends on the codec and container.
The read interrupt deadline also includes the capture duration boundary. If that
boundary interrupts input after packets have been written, the file is finalized
and returned with `DurationLimit`. A limit reached before any writable packet
returns `InvalidMedia` and removes the empty partial file. Source timeout or total
operation timeout returns `Timeout`; trailer writing remains subject to the total
operation timeout rather than the elapsed capture-duration boundary.

Timeouts and cancellation use FFmpeg's cooperative interruption mechanism and
checks between packet/decode/mux operations. Device drivers, third-party
protocols, decoding calls or blocked filesystem writes that do not honor
interruption cannot be forcibly terminated by this SDK. A caller requiring a hard
wall-clock termination boundary should run the operation in a managed worker
process. A cancellation callback must be cheap and must not re-enter the session.
If it throws, the operation returns `BackendFailure` and cleans up its output.

## Build and licensing

Build with `IICAMERA_WITH_FFMPEG=ON` to link `libavformat`, `libavcodec`,
`libavdevice`, `libavutil`, and `libswscale` through pkg-config. With the option
disabled, the same public API remains available and returns `Unavailable` for
media operations and catalogs. This is deliberate capability reporting.

`mediaBackendInfo()` reports the loaded libraries' actual versions, license
strings and codec configuration. FFmpeg licensing depends on its build options
and linked external libraries. A GPL-enabled Homebrew build makes the adapter's
redistribution obligations different from a minimal LGPL build. The SDK's own
license cannot override those dependencies, codec patents, or third-party terms.
Choose and audit the FFmpeg build used in every distributed product.

The media test generates a 64x48 FFV1 video with mono PCM audio, verifies content
recognition without trustworthy extensions, reads both packet streams, decodes
real patterned pixels, reopens and decodes remuxed output, checks file preservation
and cancellation cleanup, and decodes a red synthetic lavfi frame. Physical
camera permission, capture, lens control and target-device runtime require
separate platform/hardware verification.

Official references:

- [FFmpeg demuxing and custom IO](https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html)
- [FFmpeg codec descriptors and codec iteration](https://ffmpeg.org/doxygen/trunk/group__lavc__core.html)
- [FFmpeg licensing and legal considerations](https://ffmpeg.org/legal.html)
