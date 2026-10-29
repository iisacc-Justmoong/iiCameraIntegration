# Architecture and contracts

iiCameraIntegration separates camera descriptions, control operations, native acquisition,
and media identification. The public core is C++23 and does not require Qt. Platform
backends may depend on their operating system's camera services, and media probing may
depend on a separately detected codec provider.

Bulk inspection is documented in [Features.md](Features.md). It composes discovery,
typed control snapshots and native feature values without GUI dependencies.
Pairing feature queries share the existing revocable resource gate. Separate
Apple/Linux/Windows/Android translation units isolate OS dependencies. Private
driver-test seams are excluded from installed headers. Version 0.4 requires
consumer rebuilds because the callback object layouts have changed.

## Camera metadata

`CameraInfo` is an owned value snapshot. It includes device identity, manufacturer,
model, serial number, firmware, transport, position, connection state, sensor properties,
lenses, capture formats, the active format, and control capabilities. Sensor dimensions,
bit depth, focal length, aperture, focus distance, field of view, frame rates, pixel format,
and codec information are represented where the provider reports them.

Optional values mean that the provider has not reported the property. Empty lens or
format lists are valid when discovery cannot supply those details. Consumers must not
infer a physical aperture, sensor size, or interchangeable lens from an absent value.
Providers preserve properties without a standard field in `additional`, using provider
namespaces such as `avfoundation.device.uniqueID`. Each nested sensor, lens, format,
and control object also has its own `additional` map. Validation retains these fields.
The object can represent provider metadata without a fixed list of vendor extensions;
it cannot promise information that hardware or its driver does not expose.

`validateCameraInfo` verifies required identity fields and consistency of reported
dimensions, ranges, lens and format identifiers, the active format, and control lists.
Reported optical focal-length and aperture ranges must be positive, and their current
values must belong to the reported range. Focus distance may include zero.
Validation does not contact a device or make a discovery snapshot current. Applications
should rediscover devices or request a new backend snapshot when hardware changes.

## Lens and exposure controls

`ControlCapability` describes a numeric value, a mode, or a string identifier. Capability
flags distinguish readable controls from writable controls. Numeric ranges can be
continuous, stepped from their minimum, or an explicit discrete list. A discrete list is
authoritative when present. Each command is checked against the captured capability
before a provider callback runs. Unknown controls, read-only commands, wrong value
types, nonfinite numbers, unsupported modes or identifiers, and out-of-range numeric
commands produce an error without dispatching to the provider.

`ControlPort` owns a copy of its capabilities and callbacks. Capturing a shared backend
object in a callback keeps that backend alive for the port's lifetime, including copies
of the port. The provider determines the actual applied value and returns it to the
caller. Provider errors retain their category, message, and native error code. Exceptions
from callbacks become `BackendFailure` results.

Capabilities are a snapshot, so the provider must revalidate the live device before
reading or applying a command. The core does not assume that a digital zoom is an
optical focal-length change, or that selecting an aperture is possible on every camera.
Applications must inspect capabilities before offering those controls. Atomic batches,
thread safety, and serialization are provider responsibilities; `ControlPort` itself does
not schedule hardware operations or add synchronization.

## Optical geometry

The optical helpers compute rectilinear field of view from physical sensor dimensions
and focal length, and the inverse focal length from horizontal field of view. Dimensions
use millimeters and angles use degrees. The forward relationship is
`angle = 2 * atan(sensor_dimension / (2 * focal_length))`; diagonal geometry uses the
sensor diagonal. These helpers apply to a rectilinear lens. Fisheye projection,
distortion, stabilization crop, digital zoom, and active sensor-area crop require
provider-specific data and are not inferred from nominal dimensions.

Nonfinite or nonpositive dimensions are rejected. The inverse accepts an angle strictly
between zero and 180 degrees. Values whose result cannot be represented as a finite,
positive optical quantity also return `InvalidArgument`.

## Media identity and acquisition

The FFmpeg-backed API reports codec identities separately from the decoders and
encoders available in the current provider build. Format discovery reports the
installed demuxer and muxer inventory. Content probing supplies container identity,
individual stream codec IDs and names, time bases, dimensions, audio properties,
duration where available, and provider metadata. `probeMediaBytes` uses caller-owned
bytes synchronously through a seekable input without an extension hint. The caller
must keep those bytes valid until the call returns.

`MediaSource` accepts a file, URL, device location, or supported input graph. Automatic
content identification is appropriate for self-describing media. Native device inputs
and raw streams may require an explicit input format and provider-specific options.
Unconsumed input options are returned in metadata so that applications can detect
unsupported or misspelled settings.

`PacketCapture` owns a native input and returns owned packet payloads. Each packet
retains its stream index, presentation and decoding timestamps, duration, key-frame
flag, and the stream's time base. The session is movable and requires serialization
for reading and metadata access. `cancel()` may run concurrently with a read; provider
interruption is cooperative. A moved-from session reports an error on reading.

`decodeRgbFrame` decodes one selected video frame and converts it to an owned RGB24
buffer. It limits packets, pixels, and operation time. `recordMedia` remuxes compatible
compressed streams into a local single file, preserving codec identity instead of
transcoding. It creates the destination exclusively, refuses multi-file muxers, and
removes an uncommitted destination after failure. Callers should inspect the reported
stop reason, packet count, and payload and file sizes. Duration, packet, and payload
limits can produce a successful shorter recording.

## Evidence boundaries

Unit tests exercise metadata preservation and rejection, numeric ranges, control
preflight and provider contracts, and independently specified optical reference values.
Generated PNG, JPEG, WAV, FLAC, MP4, WebM, and Matroska fixtures exercise content
recognition from a path, caller-owned bytes, and a path with a misleading `.txt`
extension. The tests identify their expected PNG, MJPEG, PCM, FLAC, H.264, AAC, VP9,
Opus, and FFV1 codecs and verify stream dimensions, audio properties, time bases, and
container metadata. This sample set establishes behavior for those fixtures; codec and
format catalogs describe the detected provider's broader inventory without claiming
that every possible format, codec profile, or damaged file has been tested.
`StreamInfo::parametersComplete` and `MediaInfo::streamParametersComplete` distinguish
recognized identity from the presence of usable stream parameters. For example, a
PNG signature alone can identify PNG while leaving image dimensions unavailable.
Completeness reports required parameter presence; it does not establish integrity of
the complete file, successful decoding of all frames, or an undamaged media payload.
The installed consumer checks package discovery, public header composition, linkage,
and optical calls through the installed CMake target. These checks do not establish
permission to a physical camera, successful hardware control, or a real capture. Native
device and media-provider checks must report their actual available platform,
dependencies, and devices separately.
