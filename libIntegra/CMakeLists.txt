# libIntegra - the Integra modular audio framework (GPL-2.0-or-later)

file(GLOB LIBINTEGRA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB LIBINTEGRA_PUBLIC_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/api/*.h)

configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

add_library(Integra STATIC ${LIBINTEGRA_SOURCES} ${LIBINTEGRA_PUBLIC_HEADERS})
add_library(Integra::Integra ALIAS Integra)

target_include_directories(Integra
    PUBLIC
        # the public headers are included both as "api/foo.h" and as "foo.h"
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/api
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_compile_definitions(Integra
    PUBLIC INTEGRA_STATIC
    PRIVATE LIBINTEGRA_EXPORTS LIBINTEGRA_VERSION=${INTEGRA_VERSION_STRING}
            $<$<PLATFORM_ID:Windows>:NOMINMAX _CRT_SECURE_NO_WARNINGS>)

target_link_libraries(Integra
    PUBLIC  Threads::Threads
    PRIVATE integra_libpd integra_pdexternals integra_minizip integra_tmpfileplus
            LibXml2::LibXml2 integra::lua integra::portaudio integra::portmidi
            ${CMAKE_DL_LIBS})

# Runtime resources (XML schema and legacy id table) are looked up relative to
# the binary; INTEGRA_RESOURCES_DIR is the build-tree fallback.
set(INTEGRA_RESOURCES_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data CACHE INTERNAL "")
target_compile_definitions(Integra PRIVATE INTEGRA_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/")
