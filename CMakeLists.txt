cmake_minimum_required(VERSION 3.22)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/libIntegra/VERSION.txt" INTEGRA_VERSION_STRING)
string(STRIP "${INTEGRA_VERSION_STRING}" INTEGRA_VERSION_STRING)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" INTEGRA_VERSION_TRIPLE "${INTEGRA_VERSION_STRING}")

project(IntegraLive VERSION ${INTEGRA_VERSION_TRIPLE} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(INTEGRA_BUILD_TESTS "Build libIntegra unit tests (GoogleTest)" ON)
option(INTEGRA_BUILD_APP "Build the Integra Live JUCE application" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(IntegraDependencies)

add_subdirectory(third_party)
add_subdirectory(libIntegra)

if(INTEGRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libIntegra/tests)
endif()

if(INTEGRA_BUILD_APP)
    add_subdirectory(app)
endif()
