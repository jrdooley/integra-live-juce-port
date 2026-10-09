# libIntegra unit tests (GoogleTest), originally written for the Xcode/MSVC builds
# in integra-framework.

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(libIntegraTests main.cpp util.cpp)
target_link_libraries(libIntegraTests PRIVATE Integra GTest::gtest)
target_compile_definitions(libIntegraTests PRIVATE
    INTEGRA_TEST_MODULE_DIR="${PROJECT_SOURCE_DIR}/modules"
    INTEGRA_TEST_VERSION_FILE="${PROJECT_SOURCE_DIR}/libIntegra/VERSION")

# The tests load/save .integra files relative to the working directory, so run
# them from a scratch copy of the test resources.
set(_test_workdir ${CMAKE_CURRENT_BINARY_DIR}/workdir)
add_custom_command(TARGET libIntegraTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_test_workdir}/third_party
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${_test_workdir})

add_test(NAME libIntegraTests COMMAND libIntegraTests WORKING_DIRECTORY ${_test_workdir})
set_tests_properties(libIntegraTests PROPERTIES ENVIRONMENT "INTEGRA_RESOURCES_PATH=${PROJECT_SOURCE_DIR}/libIntegra/data")

add_executable(integra_smoke smoke_test.cpp)
target_link_libraries(integra_smoke PRIVATE Integra)
add_test(NAME integra_smoke
    COMMAND integra_smoke ${PROJECT_SOURCE_DIR}/modules ${PROJECT_SOURCE_DIR}/blocks
    WORKING_DIRECTORY ${_test_workdir})
set_tests_properties(integra_smoke PROPERTIES ENVIRONMENT "INTEGRA_RESOURCES_PATH=${PROJECT_SOURCE_DIR}/libIntegra/data")

add_executable(integra_host_audio_test host_audio_test.cpp)
target_link_libraries(integra_host_audio_test PRIVATE Integra)
add_test(NAME integra_host_audio_test
    COMMAND integra_host_audio_test ${PROJECT_SOURCE_DIR}/modules
    WORKING_DIRECTORY ${_test_workdir})
set_tests_properties(integra_host_audio_test PROPERTIES ENVIRONMENT "INTEGRA_RESOURCES_PATH=${PROJECT_SOURCE_DIR}/libIntegra/data")
