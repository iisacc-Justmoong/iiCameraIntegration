if(NOT IICAMERA_SOURCE)
    message(FATAL_ERROR "IICAMERA_SOURCE is required")
endif()
if(EXISTS "${IICAMERA_SOURCE}/src/iiCameraIntegration")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()
foreach(_file Camera.h Camera.cpp Types.h iiCameraIntegration.h
        Devices/Devices.h Devices/Devices.cpp Features/Features.h Features/Features.cpp
        Media/Media.h Media/Media.cpp Platform/NativeCamera.h Platform/NativeCamera.mm
        Platform/NativeCameraAndroid.cpp Platform/NativeCameraLinux.cpp
        Platform/NativeCameraWindows.cpp Platform/NativeCameraUnavailable.cpp
        Platform/V4L2.cpp detail/Identifiers.h detail/V4L2.h)
    if(NOT EXISTS "${IICAMERA_SOURCE}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
