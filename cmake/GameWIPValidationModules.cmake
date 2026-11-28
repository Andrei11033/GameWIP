include(CMakeParseArguments)

# Provides registration helpers for correctness-test and benchmark modules.
#
# Public helpers:
# - gamewip_add_validation_module_directories(...)
# - gamewip_add_test_module(...)
# - gamewip_add_benchmark_module(...)
#
# Correctness modules receive CTest entries; benchmark modules remain separate
# from correctness-test thresholds while both contribute to aggregate targets.

function(gamewip_add_validation_module_directories root_directory)
    file(GLOB gamewip_module_cmake_files CONFIGURE_DEPENDS "${root_directory}/*/CMakeLists.txt")
    list(SORT gamewip_module_cmake_files)

    foreach(gamewip_module_cmake_file IN LISTS gamewip_module_cmake_files)
        get_filename_component(gamewip_module_directory "${gamewip_module_cmake_file}" DIRECTORY)
        get_filename_component(gamewip_module_name "${gamewip_module_directory}" NAME)

        # Shared support folders do not define validation modules.
        if(gamewip_module_name STREQUAL "internal" OR gamewip_module_name STREQUAL "platform")
            continue()
        endif()

        add_subdirectory("${gamewip_module_directory}" "${gamewip_module_name}")
    endforeach()
endfunction()

function(gamewip_add_test_module)
    cmake_parse_arguments(PARSE_ARGV 0 MODULE "" "NAME" "SOURCES;LINK_LIBRARIES")

    if(MODULE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gamewip_add_test_module received unknown arguments: ${MODULE_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT MODULE_NAME OR NOT MODULE_SOURCES)
        message(FATAL_ERROR "gamewip_add_test_module requires NAME and SOURCES.")
    endif()

    set(gamewip_test_module_target "GameWIPTestModule_${MODULE_NAME}")

    if(TARGET "${gamewip_test_module_target}")
        message(FATAL_ERROR "Validation module target already exists: ${gamewip_test_module_target}")
    endif()

    add_library("${gamewip_test_module_target}" OBJECT ${MODULE_SOURCES})
    target_compile_features("${gamewip_test_module_target}" PRIVATE cxx_std_23)
    target_include_directories("${gamewip_test_module_target}" PRIVATE "${PROJECT_SOURCE_DIR}/game")
    target_link_libraries("${gamewip_test_module_target}" PRIVATE GameWIPValidationProcess Terminal ${MODULE_LINK_LIBRARIES})
    set_property(TARGET "${gamewip_test_module_target}" PROPERTY GAMEWIP_VALIDATION_MODULE_NAME "${MODULE_NAME}")
    set_property(GLOBAL APPEND PROPERTY GAMEWIP_VALIDATION_MODULE_TARGETS "${gamewip_test_module_target}")
    set(GAMEWIP_CURRENT_TEST_MODULE_TARGET "${gamewip_test_module_target}" PARENT_SCOPE)

    if(GAMEWIP_BUILD_TESTS)
        add_test(
            NAME "validation.tests.${MODULE_NAME}"
            COMMAND $<TARGET_FILE:GameWIPTests> "--test-module=${MODULE_NAME}" "--test-report=logs/validation/${MODULE_NAME}_test_report.txt"
        )
        set_tests_properties(
            "validation.tests.${MODULE_NAME}"
            PROPERTIES LABELS "validation;${MODULE_NAME}" TIMEOUT 300 WORKING_DIRECTORY "$<TARGET_FILE_DIR:GameWIPTests>"
        )
    endif()
endfunction()

function(gamewip_add_benchmark_module)
    cmake_parse_arguments(PARSE_ARGV 0 MODULE "" "NAME" "SOURCES;LINK_LIBRARIES")

    if(MODULE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gamewip_add_benchmark_module received unknown arguments: ${MODULE_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT MODULE_NAME OR NOT MODULE_SOURCES)
        message(FATAL_ERROR "gamewip_add_benchmark_module requires NAME and SOURCES.")
    endif()

    target_sources(GameWIPBenchmarkModules PRIVATE ${MODULE_SOURCES})
    target_link_libraries(GameWIPBenchmarkModules PRIVATE ${MODULE_LINK_LIBRARIES})
endfunction()
