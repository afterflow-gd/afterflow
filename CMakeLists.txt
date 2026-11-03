cmake_minimum_required(VERSION 3.29)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Only set Apple architectures when actually targeting Apple platforms.
if (APPLE)
    if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Afterflow VERSION 2.2.0)

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# The mod does not use C++ modules, so disable CMake's module dependency
# scanning. On Android this otherwise invokes the runner's host
# clang-scan-deps, which cannot read the NDK-built precompiled header and
# fails the build before the mod sources are compiled.
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# Large translation units need /bigobj on MSVC-compatible compilers.
# These flags are intentionally not applied to other toolchains.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj /Gy /utf-8)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Stale-extraction guard
#
# Geode extracts a .geode into geode/unzipped/<mod-id>/ and only re-extracts
# when the version string changes. A rebuild without a version bump therefore
# keeps loading the previous DLL, which makes the mod look like it did nothing.
#
# This post-build step clears the extracted cache (and the installed package
# on Windows) so the next launch always unpacks what was just built.
#
# Set -DAFTERFLOW_PURGE_UNZIPPED=OFF to disable.
# ---------------------------------------------------------------------------
option(AFTERFLOW_PURGE_UNZIPPED "Clear Geode's extracted-mod cache after each build" ON)

if (AFTERFLOW_PURGE_UNZIPPED AND WIN32)
    set(AFTERFLOW_PURGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/purge-unzipped.ps1")
    if (EXISTS "${AFTERFLOW_PURGE_SCRIPT}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${AFTERFLOW_PURGE_SCRIPT}"
            COMMENT "Clearing Geode extracted-mod cache (afterflow.prsevend)"
            VERBATIM
        )
    endif()
endif()
