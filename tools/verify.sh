#!/usr/bin/env bash
set -euo pipefail
camera_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
verify_camera_variant() {
    local camera_variant="$1"
    shift
    local camera_build="$camera_root/build/$camera_variant"
    if [[ "$camera_variant" == shared ]]; then camera_build="$camera_root/build"; fi
    cmake -S "$camera_root" -B "$camera_build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=ON -DIICAMERA_BUILD_TOOLS=ON "$@"
    cmake --build "$camera_build" --parallel 3
    ctest --test-dir "$camera_build" --output-on-failure --output-junit "$camera_build/ctest-results.xml"
    cmake --install "$camera_build" --prefix "$camera_build/install"
    cmake -S "$camera_root/tests/consumer" -B "$camera_build/consumer" -G Ninja \
        -DCMAKE_PREFIX_PATH="$camera_build/install" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$camera_build/consumer" --parallel 2
    ctest --test-dir "$camera_build/consumer" --output-on-failure \
        --output-junit "$camera_build/consumer/ctest-results.xml"
}
verify_camera_variant shared -DBUILD_SHARED_LIBS=ON -DIICAMERA_WITH_FFMPEG=ON -DIICAMERA_WITH_NATIVE=ON
verify_camera_variant static -DBUILD_SHARED_LIBS=OFF -DIICAMERA_WITH_FFMPEG=ON -DIICAMERA_WITH_NATIVE=ON
verify_camera_variant core -DBUILD_SHARED_LIBS=OFF -DIICAMERA_WITH_FFMPEG=OFF -DIICAMERA_WITH_NATIVE=OFF
if [[ "${1:-}" == --sanitize ]]; then
    cmake -S "$camera_root" -B "$camera_root/build/sanitized" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DBUILD_TESTING=ON -DIICAMERA_WITH_FFMPEG=ON -DIICAMERA_WITH_NATIVE=ON \
        '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        '-DCMAKE_OBJCXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' \
        '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined'
    cmake --build "$camera_root/build/sanitized" --parallel 3
    ctest --test-dir "$camera_root/build/sanitized" --output-on-failure \
        --output-junit "$camera_root/build/sanitized/ctest-results.xml"
fi
