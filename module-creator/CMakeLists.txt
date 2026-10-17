# Integra Module Creator - JUCE application (replaces the Flex/AIR Module Creator of the
# Integra Module Developer SDK). It edits .module files; it needs neither libIntegra nor
# libpd, only JUCE (fetched by app/CMakeLists.txt) and, to edit implementations, a Pd install.

juce_add_gui_app(ModuleCreator
    PRODUCT_NAME "Integra Module Creator"
    COMPANY_NAME "Integra Live"
    BUNDLE_ID org.integralive.ModuleCreator
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    DOCUMENT_EXTENSIONS module
    HARDENED_RUNTIME_ENABLED TRUE)

file(GLOB_RECURSE MODULE_CREATOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(ModuleCreator PRIVATE ${MODULE_CREATOR_SOURCES}
    # shared with Integra Live: the look and feel and the Markdown renderer
    ${PROJECT_SOURCE_DIR}/app/Source/UI/IntegraLookAndFeel.cpp
    ${PROJECT_SOURCE_DIR}/app/Source/UI/Markdown.cpp)
target_include_directories(ModuleCreator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
    ${PROJECT_SOURCE_DIR}/app/Source/UI)

set(_module_creator_integra_live "")
if(TARGET IntegraLive)
    # "Test in Integra Live" finds the Integra Live built alongside (installed copies sit side by side)
    set(_module_creator_integra_live "$<TARGET_FILE:IntegraLive>")
endif()

target_compile_definitions(ModuleCreator PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ModuleCreator,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ModuleCreator,JUCE_VERSION>"
    MODULE_CREATOR_SOURCE_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Resources"
    MODULE_CREATOR_SOURCE_MODULES_DIR="${PROJECT_SOURCE_DIR}/modules"
    MODULE_CREATOR_BUILT_INTEGRA_LIVE="${_module_creator_integra_live}")

target_link_libraries(ModuleCreator
    PRIVATE
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(UNIX AND NOT APPLE)
    set_target_properties(ModuleCreator PROPERTIES OUTPUT_NAME integra-module-creator)
endif()

# Resources (config, templates, Pd host patches, Pd object lists, licence) next to the binary
# or inside the macOS bundle, where CreatorConfig::findResourcesDirectory looks
if(APPLE)
    set(_module_creator_resources_dest "$<TARGET_BUNDLE_CONTENT_DIR:ModuleCreator>/Resources/module-creator-resources")
else()
    set(_module_creator_resources_dest "$<TARGET_FILE_DIR:ModuleCreator>/module-creator-resources")
endif()
add_custom_command(TARGET ModuleCreator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Resources ${_module_creator_resources_dest}
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/docs/12-module-creator.md ${_module_creator_resources_dest}/module-creator-guide.md)

if(INTEGRA_BUILD_TESTS)
    # The self test keeps the modules it saved (every shipped module, and one made from each
    # template); libIntegra's smoke test then instantiates each of them through libpd.
    set(_module_creator_test_output ${CMAKE_CURRENT_BINARY_DIR}/self-test-output)
    add_test(NAME ModuleCreator_self_test COMMAND ModuleCreator --self-test)
    set_tests_properties(ModuleCreator_self_test PROPERTIES TIMEOUT 300
        ENVIRONMENT "MODULE_CREATOR_SELF_TEST_OUTPUT=${_module_creator_test_output}"
        FIXTURES_SETUP module_creator_saved_modules)
    if(TARGET integra_smoke)
        add_test(NAME ModuleCreator_saved_modules_load_in_libIntegra
            COMMAND integra_smoke ${_module_creator_test_output}/modules ${PROJECT_SOURCE_DIR}/blocks
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/libIntegra/tests/workdir)
        set_tests_properties(ModuleCreator_saved_modules_load_in_libIntegra PROPERTIES TIMEOUT 300
            ENVIRONMENT "INTEGRA_RESOURCES_PATH=${PROJECT_SOURCE_DIR}/libIntegra/data"
            FIXTURES_REQUIRED module_creator_saved_modules)
    endif()
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME ModuleCreator_gui_test COMMAND ${XVFB_RUN} -a $<TARGET_FILE:ModuleCreator> --gui-test)
    elseif(APPLE OR WIN32)
        add_test(NAME ModuleCreator_gui_test COMMAND ModuleCreator --gui-test)
    endif()
    if(TEST ModuleCreator_gui_test)
        set_tests_properties(ModuleCreator_gui_test PROPERTIES TIMEOUT 300)
    endif()
endif()

# Installed beside Integra Live, sharing its folder (so each finds the other)
include(GNUInstallDirs)
if(APPLE)
    install(TARGETS ModuleCreator BUNDLE DESTINATION .)
else()
    set(_module_creator_install_dir ${CMAKE_INSTALL_LIBDIR}/integra-live)
    install(TARGETS ModuleCreator RUNTIME DESTINATION ${_module_creator_install_dir})
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Resources/ DESTINATION ${_module_creator_install_dir}/module-creator-resources)
    install(FILES ${PROJECT_SOURCE_DIR}/docs/12-module-creator.md DESTINATION ${_module_creator_install_dir}/module-creator-resources
            RENAME module-creator-guide.md)
    if(UNIX)
        install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
                      execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ../${_module_creator_install_dir}/integra-module-creator
                                      \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/integra-module-creator)")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/integra-module-creator.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
                RENAME integra-module-creator.png)
    endif()
endif()
