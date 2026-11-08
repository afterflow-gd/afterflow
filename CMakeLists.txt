cmake_minimum_required(VERSION 3.29)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Afterflow VERSION 2.2.7)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

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

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_FILE:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
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
