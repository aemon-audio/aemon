# Aemon: free, open-source mixing and mastering plugins.
# Copyright (C) 2026 Aemon contributors. Licensed under the GNU AGPL v3 or later.
cmake_minimum_required(VERSION 3.22)

project(Aemon VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    # Static runtime so users never need the Visual C++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(AEMON_MEDIA_TOOLS "Build the website media renderer" OFF)
option(AEMON_BUILD_TESTS "Build the offline DSP test runner" ON)
set(AEMON_FORMATS "VST3" CACHE STRING "Plugin formats to build (VST3;AU;Standalone)")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Fonts, shared by every plugin.
juce_add_binary_data(AemonFonts
    HEADER_NAME BinaryData_AemonFonts.h
    NAMESPACE AemonFonts
    SOURCES
        resources/fonts/Unbounded-SemiBold.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexMono-Medium.ttf
        resources/fonts/IBMPlexSans-Regular.ttf
        resources/fonts/IBMPlexSans-Medium.ttf)

juce_add_module(shared/aemon_core)

add_subdirectory(plugins/utility)
add_subdirectory(plugins/eq)
add_subdirectory(plugins/compressor)
add_subdirectory(plugins/limiter)

if (AEMON_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if (AEMON_MEDIA_TOOLS)
    add_subdirectory(tools/media)
endif()
