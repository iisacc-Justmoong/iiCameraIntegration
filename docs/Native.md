# Native camera discovery and controls

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

The native smoke test enumerates device metadata and verifies permission remains
unchanged without opening camera input. It does not prove physical capture,
hardware controls, or runtime behavior on an iPhone or iPad.

Primary references: [Apple AVCaptureDevice](https://developer.apple.com/documentation/avfoundation/avcapturedevice),
[Apple AVCaptureDevice.Format](https://developer.apple.com/documentation/avfoundation/avcapturedevice/format),
[camera authorization](https://developer.apple.com/documentation/avfoundation/avcapturedevice/authorizationstatus(for:)),
[lens position](https://developer.apple.com/documentation/avfoundation/avcapturedevice/lensposition),
and [configuration locking](https://developer.apple.com/documentation/avfoundation/avcapturedevice/lockforconfiguration()).
