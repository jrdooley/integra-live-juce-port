# Integra Live - JUCE application (replaces the Flex/AIR GUI and integra_server)

include(FetchContent)

set(INTEGRA_JUCE_TAG "8.0.8" CACHE STRING "JUCE git tag to build against")
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${INTEGRA_JUCE_TAG}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(IntegraLive
    PRODUCT_NAME "Integra Live"
    COMPANY_NAME "Integra Live"
    BUNDLE_ID org.integralive.IntegraLive
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    DOCUMENT_EXTENSIONS integra)

file(GLOB_RECURSE INTEGRA_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(IntegraLive PRIVATE ${INTEGRA_APP_SOURCES})
target_include_directories(IntegraLive PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(IntegraLive PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IntegraLive,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IntegraLive,JUCE_VERSION>"
    INTEGRA_SOURCE_MODULES_DIR="${PROJECT_SOURCE_DIR}/modules"
    INTEGRA_SOURCE_BLOCKS_DIR="${PROJECT_SOURCE_DIR}/blocks"
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/libIntegra/data ${_integra_resources_dest}/resources)

# Headless end-to-end test of the app core (session, model, controller, audio host)
if(INTEGRA_BUILD_TESTS)
    add_test(NAME IntegraLive_self_test COMMAND IntegraLive --self-test)
    set_tests_properties(IntegraLive_self_test PROPERTIES TIMEOUT 300)
endif()
