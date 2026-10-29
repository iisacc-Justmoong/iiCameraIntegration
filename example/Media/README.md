# Camera media consumer

This separate application composes iiCameraIntegration with iiFileProvider.
It supplies seekable staged recording streams, publishes without overwriting the
destination, and discards unfinished staged output. POSIX publication requires
hard-link support in the destination filesystem. Configure it independently against installed
packages; the camera SDK itself does not discover or link the file provider.
The synthetic media test uses provider-owned temporary resources.
