# Historical pre-migration package check. The active equivalent is
# example/ImageProcessing/tests/installed_package.cmake.
file(MAKE_DIRECTORY "${OUTPUT}")
set(prefix "${OUTPUT}/install")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${prefix}" --config "${CONFIG}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
file(WRITE "${OUTPUT}/install.log" "${output}${error}")
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Photo package installation failed: ${error}")
endif()
foreach(mode Core Optional Required)
    execute_process(COMMAND "${CMAKE_COMMAND}" --fresh -S "${SOURCE}/tests/package" -B "${OUTPUT}/${mode}"
        "-DMODE=${mode}" "-DiiCameraIntegration_DIR=${prefix}/${LIBDIR}/cmake/iiCameraIntegration"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
    file(WRITE "${OUTPUT}/${mode}.log" "${output}${error}")
    if(mode STREQUAL "Required")
        if(status EQUAL 0 OR NOT "${output}${error}" MATCHES "iiGraphicAdjust")
            message(FATAL_ERROR "Required missing image dependency was not rejected")
        endif()
    elseif(NOT status EQUAL 0)
        message(FATAL_ERROR "${mode} package should work without iiGraphicAdjust: ${error}")
    endif()
endforeach()
# Consume the installed processing component with the same dependency package
# used by the SDK build. Missing-dependency checks alone miss version mismatches.
set(consumer_build "${OUTPUT}/consumer/build")
set(consumer_generator -G "${GENERATOR}")
if(MAKE_PROGRAM)
    list(APPEND consumer_generator "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --fresh -S "${SOURCE}/tests/consumer" -B "${consumer_build}"
    ${consumer_generator} "-DCMAKE_BUILD_TYPE=${CONFIG}"
    -DIICAMERA_CONSUMER_WITH_PROCESSING=ON
    "-DiiCameraIntegration_DIR=${prefix}/${LIBDIR}/cmake/iiCameraIntegration"
    "-DiiGraphicAdjust_DIR=${GRAPHIC_ADJUST_DIR}" "-DiiFileProviderNative_DIR=${FILE_PROVIDER_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
file(WRITE "${OUTPUT}/consumer-configure.log" "${output}${error}")
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed processing consumer configuration failed: ${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${CONFIG}" --parallel 2
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
file(WRITE "${OUTPUT}/consumer-build.log" "${output}${error}")
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed processing consumer build failed: ${output}${error}")
endif()
if(CMAKE_HOST_WIN32)
    get_filename_component(graphic_prefix "${GRAPHIC_ADJUST_DIR}/../../.." ABSOLUTE)
    get_filename_component(provider_prefix "${FILE_PROVIDER_DIR}/../../.." ABSOLUTE)
    set(ENV{PATH} "${prefix}/${BINDIR};${graphic_prefix}/${BINDIR};${provider_prefix}/${BINDIR};$ENV{PATH}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" -C "${CONFIG}"
    --output-on-failure --no-tests=error
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
file(WRITE "${OUTPUT}/consumer-runtime.log" "${output}${error}")
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed processing consumer execution failed: ${output}${error}")
endif()
set(image "${OUTPUT}/installed-cli.ppm")
file(REMOVE "${image}")
execute_process(COMMAND "${prefix}/${BINDIR}/${TOOL_NAME}" lavfi
    "color=c=0x404040:size=16x16:rate=10:duration=0.2" "${image}" srgb 1
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
file(WRITE "${OUTPUT}/runtime.log" "${output}${error}")
if(NOT status EQUAL 0 OR NOT EXISTS "${image}")
    message(FATAL_ERROR "Installed photo CLI failed: ${error}")
endif()
file(SIZE "${image}" size)
if(NOT size EQUAL 781)
    message(FATAL_ERROR "Installed photo CLI output has the wrong dimensions/storage")
endif()
