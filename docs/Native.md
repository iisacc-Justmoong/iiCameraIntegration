# Native camera discovery, pairing, and controls

`NativeCameraSystem` is the AVFoundation adapter on macOS, iOS, and Mac Catalyst.
Other systems compile the same public interface and return `Unavailable` until
an operating system adapter is provided. The adapter does not use Qt.

`cameras()` enumerates connected camera devices and reports the device ID, name,
manufacturer, model, position, formats, native format subtype, and frame-rate
ranges supplied by the operating system. Unknown sensor dimensions, physical
focal length, lens identity, and other undisclosed hardware facts remain unknown.
An operating system format subtype is a native media description; it does not
establish that a recording container or compressed codec can be encoded.
`pixelFormat` is populated only when CoreVideo recognizes the subtype as a
registered pixel format. Other subtypes remain preserved in the native metadata
without being guessed to be either a pixel format or a recording codec.

`permissionStatus()` only reads the application's camera authorization status.
Neither discovery nor control creation displays a permission prompt or starts a
capture session. The application is responsible for an appropriate camera privacy
usage description and requesting access through its own user flow. Applying a
control requires existing authorization.

`devices()` returns a `CameraDeviceSystem` callback port whose discovery and
pairing delegate to the native adapter. `pair(id)` resolves the exact connected
AVFoundation video-device ID again, requires existing camera authorization,
creates an `AVCaptureDeviceInput`, and attaches it to a stopped
`AVCaptureSession`. Only after the input is attached does it return a
`CameraPairing` with `PairingMethod::Session`. It never starts the session,
adds an output, requests camera permission, or captures media. This prepares
an application-owned camera input connection; it does not prove that the
hardware is exclusively reserved or that captured frames are available.

The native adapter supports `Session` only. `DeviceBond` and nonempty
provider-specific parameters return `Unsupported`: it does not perform
Bluetooth, Wi-Fi, vendor authentication, or persistent operating system bonding.
An empty ID, embedded NUL, invalid UTF-8 ID, or unknown method returns
`InvalidArgument`. A missing or disconnected exact device returns `NotFound`.
The adapter checks an existing device's permission before creating its input and
returns `PermissionDenied` when authorization is absent. Native input failures
retain their OS error code; known device-in-use errors map to `Busy`.

The pairing retains the exact native device, input, and stopped session. Its
`status()` checks the physical device's current connection and that the input
still belongs to that session; disconnection reports `Disconnected`. Its
`controls()` resolves the retained device's exact ID through the existing native
control bridge. Pairing operations and returned controls are serialized by the
public pairing handle, and a successful `unpair()` removes the input and releases
the native resources. Existing pairing control ports are revoked by the handle's
session gate after unpairing. Pairing information is a discovery-time snapshot,
whereas `status()` observes the current connection.

Control support comes from the selected device and its current active format.
macOS AVFoundation exposes focus and exposure modes but does not expose the iOS
zoom, lens position, lens aperture, ISO, or exposure-duration APIs. Those controls
are explicitly unsupported on macOS. No UVC vendor extension is implied.

On iOS and Mac Catalyst, supported devices can provide zoom, normalized lens
position, exposure duration in seconds, ISO, and automatic/manual modes. Lens
position is a unitless value in `[0, 1]`, **not** a focus distance in metres, and
the endpoint `1` does not guarantee infinity focus. Field of view is supplied by
the active format in degrees when the operating system reports it. Changing zoom
can affect field of view; the adapter does not invent a physical focal length or
promise an independently adjustable field-of-view control.

On operating systems and SDKs with the iOS 27 variable-aperture API, the adapter
uses the format's reported aperture bounds and support check. On earlier versions
the reported lens aperture is read-only. A fixed aperture stays read-only.

Each mutation rechecks the device and capability, serializes access, obtains
`lockForConfiguration`, and promptly releases it. An accepted mutation is not
proof that a frame using that setting has been captured: the operating system may
apply focus and exposure changes asynchronously. Query the resulting device state
and validate actual captured media in the host application when such proof is
required. The adapter does not wait for frame synchronization.

The native smoke test enumerates metadata, checks invalid and missing pairing
IDs, rejects unsupported bonding/parameters, and verifies permission remains
unchanged. When permission is absent it also checks that pairing an enumerated
device returns a permission error without opening an input. On a host with no
camera, these are error-path and discovery checks; they do not prove successful
physical input attachment, hot-unplug behavior, captured frames, hardware
controls, or runtime behavior on an iPhone or iPad.

Primary references: [Apple AVCaptureDevice](https://developer.apple.com/documentation/avfoundation/avcapturedevice),
[Apple AVCaptureDevice.Format](https://developer.apple.com/documentation/avfoundation/avcapturedevice/format),
[camera authorization](https://developer.apple.com/documentation/avfoundation/avcapturedevice/authorizationstatus(for:)),
[lens position](https://developer.apple.com/documentation/avfoundation/avcapturedevice/lensposition),
and [configuration locking](https://developer.apple.com/documentation/avfoundation/avcapturedevice/lockforconfiguration()).
The connection contract also uses [device input construction](https://developer.apple.com/documentation/avfoundation/avcapturedeviceinput/init(device:)),
[input attachment](https://developer.apple.com/documentation/avfoundation/avcapturesession/addinput(_:)),
and [input removal](https://developer.apple.com/documentation/avfoundation/avcapturesession/removeinput(_:)).
