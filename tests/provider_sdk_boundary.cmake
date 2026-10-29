if(NOT IICAMERA_SOURCE)
    message(FATAL_ERROR "IICAMERA_SOURCE is required")
endif()
file(READ "${IICAMERA_SOURCE}/CMakeLists.txt" cmake_source)
file(READ "${IICAMERA_SOURCE}/cmake/iiCameraIntegrationConfig.cmake.in" package_source)
file(READ "${IICAMERA_SOURCE}/src/Media/Media.cpp" media_source)
file(READ "${IICAMERA_SOURCE}/install.sh" install_source)
if(cmake_source MATCHES "find_package\\([ \\t\\n]*iiFileProvider" OR
   cmake_source MATCHES "iiFileProvider::" OR package_source MATCHES "iiFileProvider" OR
   media_source MATCHES "iiFileProvider" OR install_source MATCHES "iiFileProvider")
    message(FATAL_ERROR "Camera SDK must not discover, import or link iiFileProvider")
endif()
