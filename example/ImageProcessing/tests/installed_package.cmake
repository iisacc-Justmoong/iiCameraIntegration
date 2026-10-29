set(prefix "${BUILD}/package-check/install")
set(work "${BUILD}/package-check")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --prefix "${prefix}" --config "${CONFIG}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Adapter installation failed: ${out}${err}")
endif()
foreach(mode Core Optional Required)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/core-package" -B "${work}/${mode}"
        "-DMODE=${mode}" "-DiiCameraIntegration_DIR=${CAMERA_DIR}" "-DiiFileProviderNative_DIR=${PROVIDER_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 15)
    if(mode STREQUAL "Required")
        if(status EQUAL 0 OR NOT "${out}${err}" MATCHES "ImageProcessing")
            message(FATAL_ERROR "Camera SDK did not reject the moved required component: ${out}${err}")
        endif()
    elseif(NOT status EQUAL 0)
        message(FATAL_ERROR "Camera SDK ${mode} request failed: ${out}${err}")
    endif()
endforeach()
set(package "${prefix}/${LIBDIR}/cmake/iiCameraImageProcessing")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer" -B "${work}/missing"
    "-DiiCameraImageProcessing_DIR=${package}" "-DiiCameraIntegration_DIR=${CAMERA_DIR}"
    "-DiiFileProviderNative_DIR=${PROVIDER_DIR}" -DCMAKE_DISABLE_FIND_PACKAGE_iiGraphicAdjust=TRUE
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 15)
if(status EQUAL 0 OR NOT "${out}${err}" MATCHES "iiGraphicAdjust")
    message(FATAL_ERROR "Missing image dependency was not rejected: ${out}${err}")
endif()
set(consumer "${work}/consumer")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer" -B "${consumer}"
    -G "${GENERATOR}" "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DiiCameraImageProcessing_DIR=${package}"
    "-DiiCameraIntegration_DIR=${CAMERA_DIR}" "-DiiGraphicAdjust_DIR=${GRAPHIC_DIR}"
    "-DiiFileProviderNative_DIR=${PROVIDER_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed adapter consumer configure failed: ${out}${err}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer}" --config "${CONFIG}" --parallel 2
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed adapter consumer build failed: ${out}${err}")
endif()
if(CMAKE_HOST_WIN32)
    get_filename_component(camera_prefix "${CAMERA_DIR}/../../.." ABSOLUTE)
    get_filename_component(graphic_prefix "${GRAPHIC_DIR}/../../.." ABSOLUTE)
    get_filename_component(provider_prefix "${PROVIDER_DIR}/../../.." ABSOLUTE)
    set(ENV{PATH} "${prefix}/${BINDIR};${camera_prefix}/${BINDIR};${graphic_prefix}/${BINDIR};${provider_prefix}/${BINDIR};$ENV{PATH}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer}" -C "${CONFIG}" --output-on-failure --no-tests=error
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed adapter consumer test failed: ${out}${err}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${PROVIDER_PYTHON}"
    "${PYTHON}" "${SOURCE}/tests/cli.py" "${prefix}/${BINDIR}/${TOOL_NAME}" "${SOURCE}/resources"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 40)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Installed adapter CLI test failed: ${out}${err}")
endif()
