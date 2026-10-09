# Locates the external libraries needed by libpd, the Pd externals and libIntegra.
#
# Each dependency is exposed as an INTERFACE target in the integra:: namespace so
# the rest of the build doesn't care whether it came from pkg-config, a CMake
# package config, or FetchContent.

include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)   # FetchContent_Populate for the Lua fallback
endif()
find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibXml2 REQUIRED)

# Small helper: try pkg-config names in order, then fall back to a CMake package.
function(integra_find_pkg target)
    cmake_parse_arguments(ARG "" "CMAKE_PACKAGE" "PKG_NAMES;CMAKE_TARGET" ${ARGN})
    add_library(${target} INTERFACE IMPORTED GLOBAL)
    if(PKG_CONFIG_FOUND)
        foreach(name IN LISTS ARG_PKG_NAMES)
            pkg_check_modules(_pc_${name} QUIET IMPORTED_TARGET GLOBAL ${name})
            if(_pc_${name}_FOUND)
                target_link_libraries(${target} INTERFACE PkgConfig::_pc_${name})
                message(STATUS "Integra: ${target} via pkg-config '${name}' (${_pc_${name}_VERSION})")
                return()
            endif()
        endforeach()
    endif()
    if(ARG_CMAKE_PACKAGE)
        # package config files (e.g. from vcpkg); ports name their targets differently
        find_package(${ARG_CMAKE_PACKAGE} CONFIG QUIET)
        foreach(candidate IN LISTS ARG_CMAKE_TARGET)
            if(TARGET ${candidate})
                target_link_libraries(${target} INTERFACE ${candidate})
                message(STATUS "Integra: ${target} via CMake package ${ARG_CMAKE_PACKAGE} (${candidate})")
                return()
            endif()
        endforeach()
    endif()
    set(${target}_NOTFOUND TRUE PARENT_SCOPE)
endfunction()

# --- Lua 5.2 (libIntegra's script engine is written against the 5.2 C API) ----
integra_find_pkg(integra::lua PKG_NAMES lua5.2 lua-5.2 lua52)
if(integra::lua_NOTFOUND)
    message(STATUS "Integra: Lua 5.2 not found on system - fetching Lua 5.2.3 from github.com/lua/lua")
    FetchContent_Declare(lua52
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG v5.2.3
        GIT_SHALLOW TRUE)
    FetchContent_Populate(lua52)
    file(GLOB _lua_sources "${lua52_SOURCE_DIR}/*.c")
    list(FILTER _lua_sources EXCLUDE REGEX "/(lua|luac|ltests)\\.c$")
    add_library(integra_lua_static STATIC ${_lua_sources})
    target_include_directories(integra_lua_static PUBLIC "${lua52_SOURCE_DIR}")
    # libIntegra uses luaL_register (5.1 API kept in 5.2 behind the compat switch)
    target_compile_definitions(integra_lua_static PUBLIC LUA_COMPAT_ALL)
    if(UNIX)
        target_compile_definitions(integra_lua_static PRIVATE LUA_USE_POSIX)
        target_link_libraries(integra_lua_static PUBLIC m)
    endif()
    target_link_libraries(integra::lua INTERFACE integra_lua_static)
endif()

# --- libsndfile (fsplay~) -------------------------------------------------------
integra_find_pkg(integra::sndfile PKG_NAMES sndfile CMAKE_PACKAGE SndFile CMAKE_TARGET SndFile::sndfile)
if(integra::sndfile_NOTFOUND)
    message(FATAL_ERROR "libsndfile not found (install libsndfile1-dev / brew install libsndfile / vcpkg libsndfile)")
endif()

# --- FFTW3 single precision (partconv~) -----------------------------------------
integra_find_pkg(integra::fftw3f PKG_NAMES fftw3f CMAKE_PACKAGE FFTW3f CMAKE_TARGET FFTW3::fftw3f)
if(integra::fftw3f_NOTFOUND)
    message(FATAL_ERROR "FFTW3 (float) not found (install libfftw3-dev / brew install fftw / vcpkg fftw3)")
endif()

# --- PortAudio ------------------------------------------------------------------
integra_find_pkg(integra::portaudio PKG_NAMES portaudio-2.0 CMAKE_PACKAGE portaudio CMAKE_TARGET portaudio_static portaudio PortAudio::portaudio)
if(integra::portaudio_NOTFOUND)
    message(STATUS "Integra: PortAudio not found on system - fetching v19.7.0")
    set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(portaudio
        GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
        GIT_TAG v19.7.0)
    FetchContent_MakeAvailable(portaudio)
    target_link_libraries(integra::portaudio INTERFACE portaudio_static)
endif()

# --- PortMidi -------------------------------------------------------------------
integra_find_pkg(integra::portmidi PKG_NAMES portmidi CMAKE_PACKAGE PortMidi CMAKE_TARGET PortMidi::portmidi portmidi)
if(integra::portmidi_NOTFOUND)
    find_path(PORTMIDI_INCLUDE_DIR portmidi.h)
    find_library(PORTMIDI_LIBRARY NAMES portmidi)
    find_library(PORTTIME_LIBRARY NAMES porttime)
    if(PORTMIDI_INCLUDE_DIR AND PORTMIDI_LIBRARY)
        message(STATUS "Integra: integra::portmidi via find_library (${PORTMIDI_LIBRARY})")
        target_include_directories(integra::portmidi INTERFACE ${PORTMIDI_INCLUDE_DIR})
        target_link_libraries(integra::portmidi INTERFACE ${PORTMIDI_LIBRARY})
        if(PORTTIME_LIBRARY)
            target_link_libraries(integra::portmidi INTERFACE ${PORTTIME_LIBRARY})
        endif()
    else()
        message(STATUS "Integra: PortMidi not found on system - fetching v2.0.4")
        set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        FetchContent_Declare(portmidi
            GIT_REPOSITORY https://github.com/PortMidi/portmidi.git
            GIT_TAG v2.0.4)
        FetchContent_MakeAvailable(portmidi)
        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
        target_link_libraries(integra::portmidi INTERFACE portmidi)
    endif()
endif()
