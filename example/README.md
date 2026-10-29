# Camera integration examples

The installed package is `iiCameraIntegration`; all examples use the C++ namespace
`iiCameraIntegration` and headers below `iiCameraIntegration/`.

`tests/consumer/main.cpp` is the standalone C++23 example for optics, an artificial
camera provider, pairing, control revocation and optional image processing.
Build it against an installation with:

```sh
cmake -S tests/consumer -B build/example -G Ninja \
  -DiiCameraIntegration_DIR="$HOME/.local/SDK/iiCameraIntegration/lib/cmake/iiCameraIntegration"
cmake --build build/example
ctest --test-dir build/example --output-on-failure
```

`Media` is a separately configured camera and file-provider consumer with a
command-line example. `iicamera-inspect --help`
lists operations; the synthetic `lavfi` frame examples in the main README need
no physical camera or permission prompt.
