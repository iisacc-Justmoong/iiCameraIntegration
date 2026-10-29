# Camera discovery and pairing

`Devices.h` provides synchronous camera discovery and single-owner pairing.
Discovery returns validated `CameraInfo` snapshots. Pairing re-discovers the
requested ID, verifies that it is connected, and checks that the provider's
handle identifies that same device, uses the requested pairing method, and is
ready. The library does not treat discovering a camera as connecting to it.

The API uses C++23 and standard-library callbacks. It is part of the core
`iiCameraIntegration::iiCameraIntegration` target and does not require the
optional image-processing component or FFmpeg.

## Native camera use

```cpp
#include <iiCameraIntegration/NativeCamera.h>
#include <iostream>

using namespace iiCameraIntegration;

int prepareFirstCamera() {
    NativeCameraSystem native;
    auto devices = native.devices();
    auto discovered = devices.discover();
    if (!discovered) {
        std::cerr << discovered.error().message << '\n';
        return 1;
    }
    if (discovered->empty()) return 0;

    // The host application must already have camera permission.
    // A Session prepares input resources without starting capture.
    auto pairing = devices.pair(discovered->front().id);
    if (!pairing) {
        std::cerr << pairing.error().message << '\n';
        return 1;
    }
    auto info = pairing->info();
    if (!info) return 1;
    std::cout << info->device.name << '\n';

    // Obtain a control port while the pairing is live. Its supported
    // capabilities are available through controls->capabilities().
    auto controls = pairing->controls();
    if (!controls) return 1;

    auto released = pairing->unpair();
    if (!released) {
        std::cerr << released.error().message << '\n';
        return 1; // Destruction makes one final best-effort release attempt.
    }
    return 0;
}
```

On supported Apple platforms, native discovery uses AVFoundation. Session
pairing creates an `AVCaptureDeviceInput` and attaches it to a stopped
`AVCaptureSession`. It does not request privacy permission, start a capture
stream, capture a photograph, or expose the native session as a media source.
Applications own their permission prompt and camera usage description. The
existing media capture APIs retain their separate source and lifetime contract.

`PairingMethod::Session` means preparing an SDK/provider session.
`PairingMethod::DeviceBond` means actual device authentication or bonding and
requires a provider that implements it. The native AVFoundation provider returns
`Unsupported` for device bonding; the API does not promise Bluetooth, Wi-Fi,
USB authorization, or operating-system pairing. Other unsupported platforms
return `Unavailable` through their fallback provider.

The Linux provider retains an open V4L2 descriptor; Windows retains an unstarted
Media Foundation source; Android API 24+ opens a Camera2 device without creating
a capture session. These native session providers also reject bonding/parameters.
All providers share the revocable pairing lifetime. Bulk `features()` inspection
and optional provider callbacks are described in [Features.md](Features.md).

The native session provider accepts an empty `PairingOptions::parameters` map.
Nonempty native parameters return `Unsupported`; no undocumented authentication
or configuration parameter is silently accepted.

## Provider integration

`CameraDeviceSystem` is a concrete callback port. A USB, network, or vendor SDK
can supply its discovery and pairing implementation without inheriting an SDK
base class. IDs must be nonempty, valid UTF-8, contain no NUL, and be unique
within one discovery result. Their stability across hotplug or restart is the
provider's responsibility.

The following abbreviated provider demonstrates a prepared session, not device
authentication or a real hardware implementation:

```cpp
#include <iiCameraIntegration/Devices.h>
#include <memory>

using namespace iiCameraIntegration;

struct ExampleSession {
    CameraInfo camera;
    bool connected{true};
    bool released{false};
};

CameraDeviceSystem exampleDevices(CameraInfo camera) {
    return CameraDeviceSystem({
        [camera]() -> Result<std::vector<CameraInfo>> {
            return std::vector{camera};
        },
        [camera](const std::string& id, const PairingOptions& options)
            -> Result<CameraPairing> {
            if (options.method != PairingMethod::Session)
                return std::unexpected(Error{
                    ErrorCode::Unsupported, "Example provider supports sessions only"});
            if (id != camera.id)
                return std::unexpected(Error{
                    ErrorCode::NotFound, "Camera no longer exists"});

            // A real provider acquires its device input here and rechecks
            // availability after acquisition. Keep credentials in provider
            // resources; do not copy options.parameters into PairingInfo.
            auto resource = std::make_shared<ExampleSession>();
            resource->camera = camera;
            PairingInfo info{camera, options.method,
                             {{"example.session", "prepared"}}};
            return CameraPairing::create(std::move(info), {
                [resource]() -> Result<PairingState> {
                    if (resource->released) return PairingState::Unpaired;
                    return resource->connected ? PairingState::Ready
                                               : PairingState::Disconnected;
                },
                [resource]() -> Result<ControlPort> {
                    // Replace this empty port with actual capability and
                    // hardware callbacks when the provider supports controls.
                    return ControlPort({}, {});
                },
                [resource]() -> Result<void> {
                    // Release the real provider's input resources here.
                    resource->released = true;
                    return {};
                }
            });
        }
    });
}
```

The factory validates the information, connected flag, method, and all three
required callbacks. It does not call the provider's status callback. The system
performs the ready-state check after the provider returns a handle. A provider
must release any resources it acquired if its own construction fails before it
returns a valid owner. Provider exceptions become `BackendFailure` errors at API
boundaries.

`PairingOptions::parameters` is provider-specific input. The SDK forwards it to
the provider and does not insert it into pairing information. A provider is
responsible for parameter validation, secret handling, and the device's actual
authentication protocol. Discovery metadata describes only the fields the
provider can report; it does not invent hardware capabilities.

## Ownership and disconnection

`CameraPairing` is movable and noncopyable. `info()` returns a value snapshot,
so changing the returned metadata does not mutate a session. That initial
snapshot remains accessible after release; `status()` reports the live or
latched lifecycle state.

Successful `unpair()` changes the state to `Unpaired` and is idempotent. Failed
release preserves ownership so the caller can inspect the error and retry.
Destruction makes one final best-effort release attempt and suppresses provider
exceptions. Move assignment also releases the destination's previous owner.
Destruction and move assignment revoke the old gate even if their final release
attempt fails, so an escaped port cannot continue using abandoned ownership.
Applications requiring release confirmation must call `unpair()` and check its
result before destroying the owner.

A control port returned by a pairing holds a weak session gate. It rechecks the
provider's status before forwarding valid control operations and serializes the
status check with the actual hardware operation. It cannot retain a session
after the owner dies. Successful release or destruction revokes escaped ports;
otherwise valid operations return `Cancelled` without calling the provider.
Physical disconnection latches `Disconnected`, and controls return `NotFound`.
A reappearing device requires a newly established pairing. Capability preflight
still rejects unsupported or malformed commands before dispatch.

Moving from a pairing leaves an inert object whose operations return
`InvalidArgument`. The moved-to owner retains the original resources and all
previously issued control ports continue to refer to that same live session.

## Threading and error boundaries

Operations on a live pairing and its escaped control ports are serialized by
the same session gate. Calls are synchronous. Provider callbacks must not
reenter the pairing or a control port from that pairing: callbacks execute while
the gate is held. Do not move or destroy an object concurrently with operations
on that same object. The SDK does not schedule work, impose a callback timeout,
or interrupt a blocked vendor callback.

`CameraDeviceSystem` stores callbacks but does not serialize independent
discovery or pairing calls. Providers sharing resources across those calls must
synchronize them. Distinct pairing owners also have distinct gates; a provider
must protect any resources shared between their sessions and implement its own
exclusive-camera policy where required. Physical changes can still occur after
the SDK's preflight check, so the provider must validate the device again during
resource acquisition and hardware control.

| Condition | Result |
| --- | --- |
| Invalid caller ID, method, or factory information/callbacks | `InvalidArgument` |
| Missing discovery/pair callback or unavailable native backend | `Unavailable` |
| Unknown or disconnected requested device | `NotFound` |
| Invalid provider discovery/returned pairing identity, method, or ready state | `BackendFailure` |
| Unsupported device bonding | `Unsupported` from that provider |
| Missing native camera privacy permission | `PermissionDenied` |
| Escaped port after successful unpair or owner destruction | `Cancelled` for an otherwise valid operation |
| Provider error | Original error code, message, and native code retained |
| Provider exception | `BackendFailure` |

The discovery, pairing, lifecycle, controls, failures, concurrency, and validation
tests use independent artificial providers and counters. They verify ownership,
dispatch order, malformed input rejection, revocation, retry, and session
serialization. The concurrency case also pauses an active provider query with a
bounded condition-variable wait while another thread calls `unpair()`. It checks
that resource release cannot enter before the query exits, that the resulting
order is query exit followed by unpair, and that the escaped port cannot dispatch
another query after release. These fixtures are not evidence of actual hardware discovery,
device authentication, or physical camera operation.
