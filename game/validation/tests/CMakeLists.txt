add_library(GameWIPTestModules OBJECT registry.cpp runner.cpp)
target_compile_features(GameWIPTestModules PUBLIC cxx_std_23)
target_include_directories(GameWIPTestModules PUBLIC "${PROJECT_SOURCE_DIR}/game")

if(GAMEWIP_BUILD_TESTS)
    add_executable(GameWIPTests standalone_main.cpp)
    target_compile_features(GameWIPTests PRIVATE cxx_std_23)
    target_link_libraries(GameWIPTests PRIVATE GameWIPTestModules Assert)
    set_target_properties(GameWIPTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    gamewip_copy_runtime_dependencies(GameWIPTests)
    gamewip_add_runtime_dependencies_validation(GameWIPTests)

    add_test(NAME validation.tests.help COMMAND "$<TARGET_FILE:GameWIPTests>" --help)
    set_tests_properties(validation.tests.help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:;--test-module=<name>;Registered modules:" TIMEOUT 60)

    add_test(
        NAME validation.packages.installed_consumer
        COMMAND
            ${CMAKE_COMMAND} "-DPROJECT_BUILD_DIR=${CMAKE_BINARY_DIR}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DINSTALL_PREFIX=${CMAKE_BINARY_DIR}/installed-consumer/prefix"
            "-DCONSUMER_SOURCE_DIR=${PROJECT_SOURCE_DIR}/game/validation/installed_consumer"
            "-DCONSUMER_BUILD_DIR=${CMAKE_BINARY_DIR}/installed-consumer/build" "-DGENERATOR=${CMAKE_GENERATOR}" "-DBUILD_TYPE=$<CONFIG>"
            "-DPACKAGE_VERSION=${PROJECT_VERSION}" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}"
            "-DMULTI_CONFIG=$<BOOL:${CMAKE_CONFIGURATION_TYPES}>" "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCOVERAGE_ENABLED=${GAMEWIP_ENABLE_COVERAGE}" "-DADDRESS_SANITIZER_ENABLED=${GAMEWIP_ENABLE_ADDRESS_SANITIZER}"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}" -P "${PROJECT_SOURCE_DIR}/cmake/RunInstalledConsumerValidation.cmake"
    )
    set_tests_properties(
        validation.packages.installed_consumer
        PROPERTIES LABELS "validation;packages" RUN_SERIAL TRUE TIMEOUT 600
    )

    add_test(
        NAME validation.packages.assert_interface
        COMMAND
            ${CMAKE_COMMAND} "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DWORK_DIR=${CMAKE_BINARY_DIR}/assert-interface-consumer"
            "-DCONSUMER_SOURCE_DIR=${PROJECT_SOURCE_DIR}/game/validation/installed_consumer/isolated" "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DBUILD_TYPE=$<CONFIG>" "-DPACKAGE_VERSION=${PROJECT_VERSION}" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}"
            "-DMULTI_CONFIG=$<BOOL:${CMAKE_CONFIGURATION_TYPES}>" "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}" -P "${PROJECT_SOURCE_DIR}/cmake/RunAssertInterfaceConsumerValidation.cmake"
    )
    set_tests_properties(
        validation.packages.assert_interface
        PROPERTIES LABELS "validation;packages" RUN_SERIAL TRUE TIMEOUT 600
    )

    function(gamewip_add_export_validation library allowlist allow_test_hooks)
        add_test(
            NAME "validation.exports.${library}"
            COMMAND
                ${CMAKE_COMMAND} "-DNM=${CMAKE_NM}" "-DLIBRARY_FILE=$<TARGET_LINKER_FILE:${library}>"
                "-DALLOWLIST_FILE=${PROJECT_SOURCE_DIR}/cmake/export_allowlists/${allowlist}" "-DALLOW_TEST_HOOKS=${allow_test_hooks}" -P
                "${PROJECT_SOURCE_DIR}/cmake/ValidateExportedSymbols.cmake"
        )
        set_tests_properties("validation.exports.${library}" PROPERTIES LABELS "validation;exports" TIMEOUT 120)
    endfunction()

    gamewip_add_export_validation(Terminal terminal.txt "${TERMINAL_ENABLE_TEST_HOOKS}")
    gamewip_add_export_validation(Window window.txt "${WINDOW_ENABLE_TEST_HOOKS}")
    gamewip_add_export_validation(Logger logger.txt "${LOGGER_ENABLE_TEST_HOOKS}")
    get_target_property(assert_target_type Assert TYPE)
    if(NOT assert_target_type STREQUAL "INTERFACE_LIBRARY")
        gamewip_add_export_validation(Assert assert.txt "${ASSERT_ENABLE_TEST_HOOKS}")
    endif()
endif()

gamewip_add_validation_module_directories("${CMAKE_CURRENT_SOURCE_DIR}")
