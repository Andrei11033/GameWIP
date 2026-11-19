# Provides post-build runtime dependency discovery and copying for binaries that
# need toolchain DLLs beside the target. The helper uses the active compiler
# search directory and fails through the generated dependency script when
# resolution is unsafe.

function(gamewip_copy_runtime_dependencies target_name)
    if(GAMEWIP_TARGET_PLATFORM STREQUAL "linux" AND NOT GAMEWIP_TARGET_NATIVE_ARCHITECTURE)
        return()
    endif()

    get_filename_component(gamewip_runtime_search_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(gamewip_runtime_search_dirs "${gamewip_runtime_search_dir}" ${GAMEWIP_TARGET_RUNTIME_DIRECTORIES})
    set_property(TARGET "${target_name}" APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake")

    set(gamewip_runtime_dependency_tool_arguments)
    if(CMAKE_OBJDUMP)
        list(APPEND gamewip_runtime_dependency_tool_arguments "-DGAMEWIP_OBJDUMP=${CMAKE_OBJDUMP}")
    endif()
    if(DEFINED GAMEWIP_TARGET_PLATFORM)
        list(APPEND gamewip_runtime_dependency_tool_arguments "-DGAMEWIP_TARGET_PLATFORM=${GAMEWIP_TARGET_PLATFORM}")
    endif()

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} "-DGAMEWIP_EXECUTABLE=$<TARGET_FILE:${target_name}>" "-DGAMEWIP_OUTPUT_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DGAMEWIP_RUNTIME_SEARCH_DIRS=${gamewip_runtime_search_dirs}" ${gamewip_runtime_dependency_tool_arguments} -P
            "${PROJECT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake"
        VERBATIM
    )
endfunction()

function(gamewip_add_runtime_dependencies_validation target_name)
    if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        return()
    endif()

    get_filename_component(gamewip_runtime_search_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

    add_test(
        NAME validation.cmake.runtime_dependencies
        COMMAND
            "${CMAKE_COMMAND}" "-DTEST_EXECUTABLE=$<TARGET_FILE:${target_name}>" "-DSOURCE_OUTPUT_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DVALIDATION_ROOT=${CMAKE_BINARY_DIR}/runtime-dependency-validation" "-DRUNTIME_SEARCH_DIR=${gamewip_runtime_search_dir}"
            "-DOBJDUMP=${CMAKE_OBJDUMP}" "-DCOPY_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake" -P
            "${PROJECT_SOURCE_DIR}/cmake/ValidateRuntimeDependencies.cmake"
    )
    set_tests_properties(
        validation.cmake.runtime_dependencies
        PROPERTIES LABELS "validation;cmake;runtime-dependencies" RUN_SERIAL TRUE TIMEOUT 120
    )
endfunction()
