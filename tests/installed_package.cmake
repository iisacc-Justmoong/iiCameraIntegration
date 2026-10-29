set(_work "${BUILD}/package-name-consumer")
file(REMOVE_RECURSE "${_work}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}"
    --prefix "${_work}/original" --config "${CONFIG}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" "-DPREFIX=${_work}/original"
    -P "${SOURCE}/tests/verify_install_contents.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(RENAME "${_work}/original" "${_work}/relocated")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer"
    -B "${_work}/build" -G "${GENERATOR}"
    "-DiiCameraIntegration_DIR=${_work}/relocated/${LIBDIR}/cmake/iiCameraIntegration"
    "-DCMAKE_PREFIX_PATH=${DEPENDENCY_PREFIX_PATH}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_iiFileProviderNative=ON -DCMAKE_DISABLE_FIND_PACKAGE_iiGraphicAdjust=ON
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_work}/build" --config "${CONFIG}"
    --parallel 2 COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${_work}/build"
    -C "${CONFIG}" --output-on-failure --no-tests=error COMMAND_ERROR_IS_FATAL ANY)
