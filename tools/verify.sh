#!/usr/bin/env bash
set -euo pipefail
camera_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
camera_photo=OFF
camera_sanitize=OFF
for camera_argument in "$@"; do
    case "$camera_argument" in
        --photo) camera_photo=ON ;;
        --sanitize) camera_sanitize=ON ;;
        --help)
            echo 'Usage: tools/verify.sh [--photo] [--sanitize]'
            echo '--photo also builds and verifies the sibling iiGraphicAdjust SDK and ImageProcessing component.'
            exit 0 ;;
        *) echo "Unknown option: $camera_argument" >&2; exit 2 ;;
    esac
done
graphic_root="$camera_root/../iiGraphicAdjust"
verify_variant() {
    local camera_variant="$1" camera_shared="$2" camera_ffmpeg="$3" camera_native="$4"
    local camera_build="$camera_root/build/$camera_variant"
    local camera_type=Release variant_photo=OFF
    local camera_flags='' camera_link_flags=''
    if [[ "$camera_variant" == shared ]]; then camera_build="$camera_root/build"; fi
    if [[ "$camera_variant" == sanitized ]]; then
        camera_type=Debug
        camera_flags='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer'
        camera_link_flags='-fsanitize=address,undefined'
    fi
    local camera_stage="$camera_build/install"
    local camera_consumer="$camera_build/consumer"
    local graphic_package=''
    if [[ "$camera_photo" == ON && "$camera_variant" != core ]]; then
        variant_photo=ON
        local graphic_build="$graphic_root/build/$camera_variant/build"
        local graphic_stage="$graphic_root/build/photo-$camera_variant-install"
        local graphic_consumer="$graphic_root/build/photo-$camera_variant-consumer"
        camera_stage="$camera_root/build/photo-$camera_variant-install"
        camera_consumer="$camera_root/build/photo-$camera_variant-consumer"
        if [[ "$camera_variant" == shared ]]; then
            graphic_build="$graphic_root/build"
            graphic_stage="$graphic_root/build/photo-install"
            graphic_consumer="$graphic_root/build/photo-consumer"
            camera_stage="$camera_root/build/photo-install"
            camera_consumer="$camera_root/build/photo-consumer"
        fi
        cmake -S "$graphic_root" -B "$graphic_build" -G Ninja -DCMAKE_BUILD_TYPE="$camera_type" \
            -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS="$camera_shared" \
            -DCMAKE_CXX_FLAGS="$camera_flags" -DCMAKE_EXE_LINKER_FLAGS="$camera_link_flags" \
            -DCMAKE_SHARED_LINKER_FLAGS="$camera_link_flags"
        cmake --build "$graphic_build" --parallel 3
        ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$graphic_build" --output-on-failure \
            --output-junit "$graphic_root/build/photo-$camera_variant-ctest.xml"
        cmake --install "$graphic_build" --prefix "$graphic_stage"
        graphic_package="$graphic_stage/lib/cmake/iiGraphicAdjust"
        cmake -S "$graphic_root/tests/consumer" -B "$graphic_consumer" -G Ninja \
            -DiiGraphicAdjust_DIR="$graphic_package" -DCMAKE_BUILD_TYPE="$camera_type" \
            -DCMAKE_CXX_FLAGS="$camera_flags" -DCMAKE_EXE_LINKER_FLAGS="$camera_link_flags"
        cmake --build "$graphic_consumer" --parallel 2
        ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$graphic_consumer" --output-on-failure \
            --output-junit "$graphic_consumer/ctest.xml"
    fi
    cmake -S "$camera_root" -B "$camera_build" -G Ninja -DCMAKE_BUILD_TYPE="$camera_type" \
        -DBUILD_TESTING=ON -DIICAMERA_BUILD_TOOLS=OFF -DBUILD_SHARED_LIBS="$camera_shared" \
        -DIICAMERA_WITH_FFMPEG="$camera_ffmpeg" -DIICAMERA_WITH_NATIVE="$camera_native" \
        -DIICAMERA_WITH_IMAGE_PROCESSING=OFF \
        -DCMAKE_CXX_FLAGS="$camera_flags" -DCMAKE_OBJCXX_FLAGS="$camera_flags" \
        -DCMAKE_EXE_LINKER_FLAGS="$camera_link_flags" -DCMAKE_SHARED_LINKER_FLAGS="$camera_link_flags"
    cmake --build "$camera_build" --parallel 3
    ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$camera_build" --output-on-failure \
        --output-junit "$camera_build/ctest-results.xml"
    cmake --install "$camera_build" --prefix "$camera_stage"
    local media_build="$camera_build/media-consumer"
    cmake -S "$camera_root/example/Media" -B "$media_build" -G Ninja \
        -DCMAKE_BUILD_TYPE="$camera_type" -DBUILD_TESTING=ON \
        -DiiCameraIntegration_DIR="$camera_stage/lib/cmake/iiCameraIntegration" \
        -DCMAKE_CXX_FLAGS="$camera_flags" -DCMAKE_EXE_LINKER_FLAGS="$camera_link_flags"
    cmake --build "$media_build" --parallel 2
    if [[ "$camera_ffmpeg" == ON ]]; then
        ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$media_build" --output-on-failure \
            --output-junit "$media_build/ctest-results.xml"
    fi
    cmake --install "$media_build" --prefix "$camera_stage"
    if [[ "$variant_photo" == ON ]]; then
        local photo_build="$camera_build/image-consumer"
        cmake -S "$camera_root/example/ImageProcessing" -B "$photo_build" -G Ninja \
            -DCMAKE_BUILD_TYPE="$camera_type" -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS="$camera_shared" \
            -DiiCameraIntegration_DIR="$camera_stage/lib/cmake/iiCameraIntegration" \
            -DiiGraphicAdjust_DIR="$graphic_package" \
            -DCMAKE_CXX_FLAGS="$camera_flags" -DCMAKE_EXE_LINKER_FLAGS="$camera_link_flags" \
            -DCMAKE_SHARED_LINKER_FLAGS="$camera_link_flags"
        cmake --build "$photo_build" --parallel 3
        ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$photo_build" --output-on-failure \
            --output-junit "$photo_build/ctest-results.xml"
        cmake --install "$photo_build" --prefix "$camera_stage"
    fi
    cmake -S "$camera_root/tests/consumer" -B "$camera_consumer" -G Ninja \
        -DiiCameraIntegration_DIR="$camera_stage/lib/cmake/iiCameraIntegration" \
        -DiiCameraImageProcessing_DIR="$camera_stage/lib/cmake/iiCameraImageProcessing" \
        -DIICAMERA_CONSUMER_WITH_PROCESSING="$variant_photo" -DiiGraphicAdjust_DIR="$graphic_package" \
        -DCMAKE_BUILD_TYPE="$camera_type" -DCMAKE_CXX_FLAGS="$camera_flags" \
        -DCMAKE_EXE_LINKER_FLAGS="$camera_link_flags"
    cmake --build "$camera_consumer" --parallel 2
    ASAN_OPTIONS=detect_leaks=0 ctest --test-dir "$camera_consumer" --output-on-failure \
        --output-junit "$camera_consumer/ctest.xml"
}
verify_variant shared ON ON ON
verify_variant static OFF ON ON
verify_variant core OFF OFF OFF
if [[ "$camera_sanitize" == ON ]]; then verify_variant sanitized ON ON ON; fi
