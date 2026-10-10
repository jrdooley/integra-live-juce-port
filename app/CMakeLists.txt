# Integra Live - JUCE application (replaces the Flex/AIR GUI and integra_server)

include(FetchContent)

set(INTEGRA_JUCE_TAG "8.0.8" CACHE STRING "JUCE git tag to build against")
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${INTEGRA_JUCE_TAG}
    GIT_SHALLOW TRUE)
# Populate + EXCLUDE_FROM_ALL so JUCE's own install rules (juceaide, headers,
# CMake package) don't end up in our installation.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(JUCE)
if(NOT juce_POPULATED)
    FetchContent_Populate(JUCE)
    add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

juce_add_gui_app(IntegraLive
    PRODUCT_NAME "Integra Live"
    COMPANY_NAME "Integra Live"
    BUNDLE_ID org.integralive.IntegraLive
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    DOCUMENT_EXTENSIONS integra module bundle
    # macOS: audio input needs a usage string (the app is killed without one) and,
    # under the hardened runtime required for notarisation, the audio-input entitlement
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Integra Live processes audio from your audio interface or microphone."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

file(GLOB_RECURSE INTEGRA_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(IntegraLive PRIVATE ${INTEGRA_APP_SOURCES})
target_include_directories(IntegraLive PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(IntegraLive PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1      # the --gui-test harness pumps the message loop
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IntegraLive,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IntegraLive,JUCE_VERSION>"
    INTEGRA_SOURCE_MODULES_DIR="${PROJECT_SOURCE_DIR}/modules"
    INTEGRA_SOURCE_BLOCKS_DIR="${PROJECT_SOURCE_DIR}/blocks"
    INTEGRA_SOURCE_VIEWINFO_DIR="${PROJECT_SOURCE_DIR}/viewInfo"
    INTEGRA_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets"
    INTEGRA_SOURCE_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/libIntegra/data")

target_link_libraries(IntegraLive
    PRIVATE
        Integra
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(UNIX AND NOT APPLE)
    # "Integra Live" is fine for a macOS bundle but awkward as a Linux binary name
    set_target_properties(IntegraLive PROPERTIES OUTPUT_NAME integra-live)
endif()

# Ship the module library, demo blocks and libIntegra resources next to the binary
# (Linux/Windows) or inside the bundle (macOS), where IntegraSession looks for them.
if(APPLE)
    set(_integra_resources_dest "$<TARGET_BUNDLE_CONTENT_DIR:IntegraLive>/Resources")
else()
    set(_integra_resources_dest "$<TARGET_FILE_DIR:IntegraLive>")
endif()
add_custom_command(TARGET IntegraLive POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/modules ${_integra_resources_dest}/modules
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/blocks ${_integra_resources_dest}/blocks
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/viewInfo ${_integra_resources_dest}/viewInfo
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets ${_integra_resources_dest}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/libIntegra/data ${_integra_resources_dest}/resources)

# Headless end-to-end test of the app core (session, model, controller, audio host)
if(INTEGRA_BUILD_TESTS)
    add_test(NAME IntegraLive_self_test COMMAND IntegraLive --self-test)
    set_tests_properties(IntegraLive_self_test PROPERTIES TIMEOUT 300)
endif()

# GUI interaction test: drives the real components with synthesized events.
# Needs a display; on headless machines it runs under xvfb-run when available.
if(INTEGRA_BUILD_TESTS)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME IntegraLive_gui_test COMMAND ${XVFB_RUN} -a $<TARGET_FILE:IntegraLive> --quiet --gui-test)
    elseif(APPLE OR WIN32)
        add_test(NAME IntegraLive_gui_test COMMAND IntegraLive --quiet --gui-test)
    endif()
    if(TEST IntegraLive_gui_test)
        set_tests_properties(IntegraLive_gui_test PROPERTIES TIMEOUT 300)
    endif()
endif()

# ------------------------------------------------------------------------------
# Installation: the binary and its data directories stay together, because
# IntegraSession looks for modules/, blocks/ and resources/ next to the executable
# (or in Contents/Resources of the macOS bundle).
include(GNUInstallDirs)
if(APPLE)
    install(TARGETS IntegraLive BUNDLE DESTINATION .)
else()
    set(_integra_install_dir ${CMAKE_INSTALL_LIBDIR}/integra-live)
    install(TARGETS IntegraLive RUNTIME DESTINATION ${_integra_install_dir})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/modules ${PROJECT_SOURCE_DIR}/blocks ${PROJECT_SOURCE_DIR}/viewInfo ${PROJECT_SOURCE_DIR}/assets DESTINATION ${_integra_install_dir})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/libIntegra/data/ DESTINATION ${_integra_install_dir}/resources)
    if(UNIX)
        install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
                      execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ../${_integra_install_dir}/integra-live
                                      \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/integra-live)")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Resources/integra-live.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps RENAME integra-live.png)
    endif()
endif()

set(CPACK_PACKAGE_NAME "integra-live")
set(CPACK_PACKAGE_VENDOR "Integra Live")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Interactive audio processing for live performance (JUCE port)")
set(CPACK_GENERATOR TGZ)
include(CPack)
