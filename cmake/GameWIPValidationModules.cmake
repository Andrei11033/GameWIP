include(CMakeParseArguments)

# Provides registration helpers for correctness-test and benchmark modules.
#
# Public helpers:
# - gamewip_add_validation_module_directories(...)
# - gamewip_add_test_module(...)
# - gamewip_add_benchmark_module(...)
#
# Contract:
# - Keep validation modules discoverable by directory.
# - Add module sources to the aggregate validation targets.
# - Register ctest entries only for correctness-test modules.
# - Keep benchmark modules separate from correctness-test thresholds.

function(gamewip_add_validation_module_directories root_directory)
    file(GLOB gamewip_module_cmake_files CONFIGURE_DEPENDS
        "${root_directory}/*/CMakeLists.txt"
    )
    list(SORT gamewip_module_cmake_files)

    foreach(gamewip_module_cmake_file IN LISTS gamewip_module_cmake_files)
        get_filename_component(gamewip_module_directory "${gamewip_module_cmake_file}" DIRECTORY)
        get_filename_component(gamewip_module_name "${gamewip_module_directory}" NAME)
        add_subdirectory("${gamewip_module_directory}" "${gamewip_module_name}")
    endforeach()
endfunction()

function(gamewip_add_test_module)
    cmake_parse_arguments(MODULE "" "NAME" "SOURCES;LINK_LIBRARIES" ${ARGN})
    if(NOT MODULE_NAME OR NOT MODULE_SOURCES)
        message(FATAL_ERROR "gamewip_add_test_module requires NAME and SOURCES.")
    endif()

    target_sources(GameWIPTestModules PRIVATE ${MODULE_SOURCES})
    target_link_libraries(GameWIPTestModules PRIVATE ${MODULE_LINK_LIBRARIES})

    if(GAMEWIP_BUILD_TESTS)
        add_test(NAME "validation.tests.${MODULE_NAME}"
            COMMAND $<TARGET_FILE:GameWIPTests>
                "--test-module=${MODULE_NAME}"
                "--test-report=logs/tests/${MODULE_NAME}_test_report.txt"
        )
        set_tests_properties("validation.tests.${MODULE_NAME}" PROPERTIES
            LABELS "validation;${MODULE_NAME}"
            TIMEOUT 300
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:GameWIPTests>"
        )
    endif()
endfunction()

function(gamewip_add_benchmark_module)
    cmake_parse_arguments(MODULE "" "NAME" "SOURCES;LINK_LIBRARIES" ${ARGN})
    if(NOT MODULE_NAME OR NOT MODULE_SOURCES)
        message(FATAL_ERROR "gamewip_add_benchmark_module requires NAME and SOURCES.")
    endif()

    target_sources(GameWIPBenchmarkModules PRIVATE ${MODULE_SOURCES})
    target_link_libraries(GameWIPBenchmarkModules PRIVATE ${MODULE_LINK_LIBRARIES})
endfunction()
