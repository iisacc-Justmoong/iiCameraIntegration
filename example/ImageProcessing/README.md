# Camera image-processing consumer

This separately configured application adapter composes iiCameraIntegration and iiGraphicAdjust. It preserves photo processing, frame conversion, synthetic tests and the optional iiFileProvider-owned PPM CLI without adding a dependency to either SDK. The installed adapter package is named `iiCameraImageProcessing`; its exported target remains `iiCameraIntegration::ImageProcessing`. The existing camera SDK component and `IICAMERA_WITH_IMAGE_PROCESSING=ON` have moved here.
