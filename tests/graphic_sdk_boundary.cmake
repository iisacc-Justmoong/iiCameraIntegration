if(NOT IICAMERA_SOURCE)
    message(FATAL_ERROR "IICAMERA_SOURCE is required")
endif()
file(READ "${IICAMERA_SOURCE}/CMakeLists.txt" cmake_source)
file(READ "${IICAMERA_SOURCE}/cmake/iiCameraIntegrationConfig.cmake.in" package_source)
if(cmake_source MATCHES "find_package\\([ \t\n]*iiGraphicAdjust" OR
   cmake_source MATCHES "iiGraphicAdjust::" OR package_source MATCHES "iiGraphicAdjust")
    message(FATAL_ERROR "Camera SDK must not discover or link the image SDK")
endif()
if(EXISTS "${IICAMERA_SOURCE}/src/iiCameraIntegration/ImageProcessing.h" OR
   EXISTS "${IICAMERA_SOURCE}/src/iiCameraIntegration/ImageProcessing.cpp")
    message(FATAL_ERROR "Cross-SDK image composition belongs to a separate consumer")
endif()
file(READ "${IICAMERA_SOURCE}/example/ImageProcessing/CMakeLists.txt" consumer_source)
if(NOT package_source MATCHES "set\\(iiCameraIntegration_WITH_FFMPEG[ \t]+@IICAMERA_WITH_FFMPEG@\\)" OR
   NOT consumer_source MATCHES "if\\(IICAMERA_COMPOSITION_BUILD_TOOLS AND iiCameraIntegration_WITH_FFMPEG\\)")
    message(FATAL_ERROR "The consumer must register FFmpeg-dependent tests only when the camera package provides FFmpeg")
endif()
