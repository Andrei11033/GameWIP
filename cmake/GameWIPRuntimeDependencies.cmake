function(gamewip_copy_runtime_dependencies target_name)
    get_filename_component(gamewip_runtime_search_dir
        "${CMAKE_CXX_COMPILER}"
        DIRECTORY
    )

    set(gamewip_runtime_dependency_tool_arguments)
    if(CMAKE_OBJDUMP)
        list(APPEND gamewip_runtime_dependency_tool_arguments
            "-DGAMEWIP_OBJDUMP=${CMAKE_OBJDUMP}"
        )
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DGAMEWIP_EXECUTABLE=$<TARGET_FILE:${target_name}>"
            "-DGAMEWIP_OUTPUT_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DGAMEWIP_RUNTIME_SEARCH_DIR=${gamewip_runtime_search_dir}"
            ${gamewip_runtime_dependency_tool_arguments}
            -P "${PROJECT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake"
        VERBATIM
    )
endfunction()

function(gamewip_add_runtime_dependencies_validation target_name)
    if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        return()
    endif()

    get_filename_component(gamewip_runtime_search_dir
        "${CMAKE_CXX_COMPILER}"
        DIRECTORY
    )

    add_test(
        NAME validation.cmake.runtime_dependencies
        COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXECUTABLE=$<TARGET_FILE:${target_name}>"
            "-DSOURCE_OUTPUT_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DVALIDATION_ROOT=${CMAKE_BINARY_DIR}/runtime-dependency-validation"
            "-DRUNTIME_SEARCH_DIR=${gamewip_runtime_search_dir}"
            "-DOBJDUMP=${CMAKE_OBJDUMP}"
            "-DCOPY_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake"
            -P "${PROJECT_SOURCE_DIR}/cmake/ValidateRuntimeDependencies.cmake"
    )
    set_tests_properties(validation.cmake.runtime_dependencies PROPERTIES
        LABELS "validation;cmake;runtime-dependencies"
        RUN_SERIAL TRUE
        TIMEOUT 120
    )
endfunction()
