string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _nonce)
set(_work "${BUILD}/package-name-consumer-${_nonce}")
function(run_checked)
    set(_command)
    math(EXPR _last "${ARGC} - 1")
    foreach(_index RANGE 0 ${_last})
        string(REPLACE ";" "\\;" _argument "${ARGV${_index}}")
        list(APPEND _command "${_argument}")
    endforeach()
    execute_process(COMMAND ${_command} RESULT_VARIABLE _result)
    if(NOT _result STREQUAL "0")
        file(REMOVE_RECURSE "${_work}")
        message(FATAL_ERROR "Installed consumer step failed: ${_result}")
    endif()
endfunction()
run_checked("${CMAKE_COMMAND}" --install "${BUILD}"
    --prefix "${_work}/original" --config "${CONFIG}")
run_checked("${CMAKE_COMMAND}" "-DPREFIX=${_work}/original"
    -P "${SOURCE}/tests/verify_install_contents.cmake")
file(RENAME "${_work}/original" "${_work}/relocated" RESULT _rename_result)
if(NOT _rename_result STREQUAL "0")
    file(REMOVE_RECURSE "${_work}")
    message(FATAL_ERROR "Installed package relocation failed: ${_rename_result}")
endif()
run_checked("${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer"
    -B "${_work}/build" -G "${GENERATOR}"
    "-DiiCameraIntegration_DIR=${_work}/relocated/${LIBDIR}/cmake/iiCameraIntegration"
    "-DCMAKE_PREFIX_PATH=${DEPENDENCY_PREFIX_PATH}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_iiFileProviderNative=ON -DCMAKE_DISABLE_FIND_PACKAGE_iiGraphicAdjust=ON)
run_checked("${CMAKE_COMMAND}" --build "${_work}/build" --config "${CONFIG}"
    --parallel 2)
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${_work}/build"
    -C "${CONFIG}" --output-on-failure --no-tests=error)

file(REMOVE_RECURSE "${_work}")
