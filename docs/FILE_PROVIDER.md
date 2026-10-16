# File creation ownership in iiCameraIntergration

Camera/media tool output and native output-directory creation delegate to the native provider. Capture/control, metadata and camera permissions remain here.

The common [ownership contract](../../iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `src/iiCameraIntergration/Media.cpp`
- `tools/inspect.cpp`
- `tools/process.cpp`
