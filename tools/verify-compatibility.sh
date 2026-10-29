#!/usr/bin/env bash
set -euo pipefail
camera_root="$(cd "$(dirname "$0")/.." && pwd)"
camera_base="$camera_root/build/compatibility"
camera_android_headers="${IICAMERA_ANDROID_TEST_INCLUDE_DIR:-}"
camera_v4l2_headers="${IICAMERA_V4L2_TEST_INCLUDE_DIR:-}"
verify_host() {
    local variant="$1" shared="$2" ffmpeg="$3" native="$4"
    local output="$camera_base/$variant/build" flags='' type=Release
    if [[ "$variant" == shared ]]; then output="$camera_root/build"; fi
    if [[ "$variant" == sanitized ]]; then
        type=Debug
        flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
    fi
    cmake -S "$camera_root" -B "$output" -G Ninja -DCMAKE_BUILD_TYPE="$type" \
        -DBUILD_SHARED_LIBS="$shared" -DBUILD_TESTING=ON \
        -DIICAMERA_WITH_FFMPEG="$ffmpeg" -DIICAMERA_WITH_NATIVE="$native" \
        -DIICAMERA_WITH_IMAGE_PROCESSING=OFF \
        -DIICAMERA_ANDROID_TEST_INCLUDE_DIR="$camera_android_headers" \
        -DIICAMERA_V4L2_TEST_INCLUDE_DIR="$camera_v4l2_headers" \
        -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_OBJCXX_FLAGS="$flags" \
        -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags"
    cmake --build "$output" --parallel 3
    ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$output" --output-on-failure --parallel 3 \
        --output-junit "$output/compatibility-ctest.xml"
    cmake --install "$output" --prefix "$camera_base/$variant/install"
    cmake -S "$camera_root/tests/consumer" -B "$camera_base/$variant/consumer/build" -G Ninja \
        -DiiCameraIntegration_DIR="$camera_base/$variant/install/lib/cmake/iiCameraIntegration" \
        -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS="$flags"
    cmake --build "$camera_base/$variant/consumer/build" --parallel 2
    ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$camera_base/$variant/consumer/build" --output-on-failure
}
case "${1:-all}" in
    shared) verify_host shared ON ON ON ;;
    static) verify_host static OFF ON ON ;;
    core) verify_host core OFF OFF OFF ;;
    sanitized) verify_host sanitized ON ON ON ;;
    all)
        verify_host shared ON ON ON
        verify_host static OFF ON ON
        verify_host core OFF OFF OFF
        verify_host sanitized ON ON ON
        ;;
    --help)
        echo 'verify-compatibility.sh [all|shared|static|core|sanitized]'
        echo 'Optional artificial driver headers: IICAMERA_{ANDROID,V4L2}_TEST_INCLUDE_DIR'
        echo 'Host variants stage under build/compatibility; no system SDK is replaced.'
        exit 0 ;;
    *) echo 'Unknown variant' >&2; exit 2 ;;
esac
