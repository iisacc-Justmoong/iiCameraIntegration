cmake_minimum_required(VERSION 3.24)
# iisdk:consumer-application
project(iiCameraImageProcessing VERSION 0.4.1 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(CTest)
option(BUILD_SHARED_LIBS "Build shared application adapter" ON)
option(IICAMERA_COMPOSITION_BUILD_TOOLS "Build the photo-processing CLI" ON)
find_package(iiCameraIntegration 0.4 CONFIG REQUIRED COMPONENTS Core)
find_package(iiGraphicAdjust 0.6 CONFIG REQUIRED)
add_library(iiCameraImageProcessing App/iiCameraIntegration/ImageProcessing.cpp)
add_library(iiCameraIntegration::ImageProcessing ALIAS iiCameraImageProcessing)
target_compile_features(iiCameraImageProcessing PUBLIC cxx_std_23)
target_link_libraries(iiCameraImageProcessing PUBLIC iiCameraIntegration::iiCameraIntegration iiGraphicAdjust::iiGraphicAdjust)
target_include_directories(iiCameraImageProcessing PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/App>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
generate_export_header(iiCameraImageProcessing BASE_NAME IICAMERAIMAGEPROCESSING
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/iiCameraIntegration/ImageProcessingExport.h")
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiCameraImageProcessing PUBLIC IICAMERAIMAGEPROCESSING_STATIC_DEFINE)
endif()
set_target_properties(iiCameraImageProcessing PROPERTIES EXPORT_NAME ImageProcessing
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
    set_target_properties(iiCameraImageProcessing PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(iiCameraImageProcessing PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
if(MSVC)
    target_compile_options(iiCameraImageProcessing PRIVATE /W4 /permissive-)
else()
    target_compile_options(iiCameraImageProcessing PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(IICAMERA_COMPOSITION_BUILD_TOOLS)
    find_package(iiFileProviderNative 0.6 CONFIG REQUIRED)
    add_executable(iiCameraProcess App/process.cpp)
    target_link_libraries(iiCameraProcess PRIVATE iiCameraIntegration::ImageProcessing iiFileProvider::Native)
    set_target_properties(iiCameraProcess PROPERTIES OUTPUT_NAME iicamera-process INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set_target_properties(iiCameraProcess PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    elseif(UNIX)
        set_target_properties(iiCameraProcess PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS iiCameraProcess RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BUILD_TESTING)
    add_executable(iiCameraPhotoProcessingTests tests/image_processing.cpp)
    target_link_libraries(iiCameraPhotoProcessingTests PRIVATE iiCameraIntegration::ImageProcessing)
    foreach(case import pipeline encoding validation capture)
        add_test(NAME iiCamera.processing.${case} COMMAND iiCameraPhotoProcessingTests ${case})
        set_tests_properties(iiCamera.processing.${case} PROPERTIES TIMEOUT 30)
    endforeach()
    if(IICAMERA_COMPOSITION_BUILD_TOOLS AND iiCameraIntegration_WITH_FFMPEG)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME iiCamera.processing.cli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.py"
            "$<TARGET_FILE:iiCameraProcess>" "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        set_tests_properties(iiCamera.processing.cli PROPERTIES TIMEOUT 40
            ENVIRONMENT "PYTHONPATH=${iiFileProviderNative_PYTHON_DIRECTORY}")
        add_test(NAME iiCamera.processing.package COMMAND "${CMAKE_COMMAND}"
            "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DLIBDIR=${CMAKE_INSTALL_LIBDIR}" "-DBINDIR=${CMAKE_INSTALL_BINDIR}"
            "-DCONFIG=$<CONFIG>" "-DTOOL_NAME=$<TARGET_FILE_NAME:iiCameraProcess>"
            "-DCAMERA_DIR=${iiCameraIntegration_DIR}" "-DGRAPHIC_DIR=${iiGraphicAdjust_DIR}"
            "-DPROVIDER_DIR=${iiFileProviderNative_DIR}" "-DPROVIDER_PYTHON=${iiFileProviderNative_PYTHON_DIRECTORY}"
            "-DPYTHON=${Python3_EXECUTABLE}" "-DGENERATOR=${CMAKE_GENERATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_package.cmake")
        set_tests_properties(iiCamera.processing.package PROPERTIES TIMEOUT 180)
    endif()
endif()
set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiCameraImageProcessing")
install(TARGETS iiCameraImageProcessing EXPORT iiCameraImageProcessingTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT iiCameraImageProcessingTargets NAMESPACE iiCameraIntegration:: DESTINATION "${package_dir}")
install(FILES App/iiCameraIntegration/ImageProcessing.h
    "${CMAKE_CURRENT_BINARY_DIR}/include/iiCameraIntegration/ImageProcessingExport.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiCameraIntegration)
configure_package_config_file(cmake/iiCameraImageProcessingConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiCameraImageProcessingConfig.cmake" INSTALL_DESTINATION "${package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiCameraImageProcessingConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiCameraImageProcessingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiCameraImageProcessingConfigVersion.cmake" DESTINATION "${package_dir}")
