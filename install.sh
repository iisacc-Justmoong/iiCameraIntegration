#!/usr/bin/env bash
set -euo pipefail
camera_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
camera_sdk_root="${IISDK_PREFIX_ROOT:-$HOME/.local/SDK}"
camera_install_prefix="${INSTALL_PREFIX:-$camera_sdk_root/iiCameraIntegration}"
camera_prefix_path="${CMAKE_PREFIX_PATH:-}"
cmake -S "$camera_root" -B "$camera_root/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DIICAMERA_BUILD_TOOLS=OFF \
    -DIICAMERA_WITH_IMAGE_PROCESSING=OFF \
    -DCMAKE_INSTALL_PREFIX="$camera_install_prefix" \
    -DCMAKE_PREFIX_PATH="$camera_prefix_path" "$@"
cmake --build "$camera_root/build" --parallel "${BUILD_JOBS:-4}"
ctest --test-dir "$camera_root/build" --output-on-failure --no-tests=error
cmake --install "$camera_root/build"
