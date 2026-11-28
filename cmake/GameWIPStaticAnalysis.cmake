# Provides static-analysis target creation for maintained C++ sources.
#
# The module creates no targets unless static analysis is enabled. Its targets
# run clang-tidy and clang-format over GameWIP-owned C++ roots while excluding
# generated and third-party headers from project-owned diagnostics.

function(gamewip_create_static_analysis_targets)
    if(NOT GAMEWIP_ENABLE_STATIC_ANALYSIS)
        return()
    endif()

    if(NOT GAMEWIP_CLANG_TIDY_JOBS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "GAMEWIP_CLANG_TIDY_JOBS must be a positive decimal integer.")
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(GAMEWIP_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    find_program(GAMEWIP_RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py REQUIRED)
    find_program(GAMEWIP_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)

    set(gamewip_owned_source_regex ".*[\\\\/](foundation|tools|engine|game)[\\\\/].*[.](c|cc|cpp|cxx)$")
    set(gamewip_owned_header_regex ".*[\\\\/](foundation|tools|engine|game)[\\\\/].*")
    set(gamewip_excluded_header_regex ".*[\\\\/](external|build)[\\\\/].*")
    get_property(gamewip_scope_scoped GLOBAL PROPERTY GAMEWIP_VALIDATION_SCOPE_SCOPED)
    get_property(gamewip_scope_full GLOBAL PROPERTY GAMEWIP_VALIDATION_SCOPE_FULL)
    get_property(gamewip_tidy_sources GLOBAL PROPERTY GAMEWIP_VALIDATION_CLANG_TIDY_SOURCES)

    set(gamewip_tidy_arguments)
    if(gamewip_scope_scoped AND NOT gamewip_scope_full)
        list(APPEND gamewip_tidy_arguments ${gamewip_tidy_sources})
    endif()

    if(gamewip_scope_scoped AND NOT gamewip_scope_full AND NOT gamewip_tidy_sources)
        add_custom_target(
            clang-tidy
            COMMAND "${CMAKE_COMMAND}" -E echo "No affected C++ translation units to analyze."
            COMMENT "Checking affected GameWIP C++ translation units"
            VERBATIM
        )
    else()
        add_custom_target(
            clang-tidy
            COMMAND
                "${Python3_EXECUTABLE}" -u "${GAMEWIP_RUN_CLANG_TIDY_EXECUTABLE}" -p "${CMAKE_BINARY_DIR}" -clang-tidy-binary
                "${GAMEWIP_CLANG_TIDY_EXECUTABLE}" -config-file "${PROJECT_SOURCE_DIR}/.clang-tidy" -source-filter "${gamewip_owned_source_regex}"
                -header-filter "${gamewip_owned_header_regex}" -exclude-header-filter "${gamewip_excluded_header_regex}" -j
                "${GAMEWIP_CLANG_TIDY_JOBS}" -quiet ${gamewip_tidy_arguments}
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Running clang-tidy on GameWIP-owned C++ sources"
            USES_TERMINAL
            VERBATIM
        )
    endif()

    file(
        GLOB_RECURSE gamewip_format_files
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/foundation/*.cpp"
        "${PROJECT_SOURCE_DIR}/foundation/*.h"
        "${PROJECT_SOURCE_DIR}/foundation/*.hpp"
        "${PROJECT_SOURCE_DIR}/foundation/*.inl"
        "${PROJECT_SOURCE_DIR}/tools/*.cpp"
        "${PROJECT_SOURCE_DIR}/tools/*.h"
        "${PROJECT_SOURCE_DIR}/tools/*.hpp"
        "${PROJECT_SOURCE_DIR}/tools/*.inl"
        "${PROJECT_SOURCE_DIR}/engine/*.cpp"
        "${PROJECT_SOURCE_DIR}/engine/*.h"
        "${PROJECT_SOURCE_DIR}/engine/*.hpp"
        "${PROJECT_SOURCE_DIR}/engine/*.inl"
        "${PROJECT_SOURCE_DIR}/game/*.cpp"
        "${PROJECT_SOURCE_DIR}/game/*.h"
        "${PROJECT_SOURCE_DIR}/game/*.hpp"
        "${PROJECT_SOURCE_DIR}/game/*.inl"
    )
    list(SORT gamewip_format_files)

    if(gamewip_scope_scoped AND NOT gamewip_scope_full)
        get_property(gamewip_format_files GLOBAL PROPERTY GAMEWIP_VALIDATION_CHANGED_CPP_FILES)
        list(SORT gamewip_format_files)
    endif()

    if(gamewip_scope_scoped AND NOT gamewip_scope_full AND NOT gamewip_format_files)
        add_custom_target(
            clang-format-check
            COMMAND "${CMAKE_COMMAND}" -E echo "No changed C++ files to format."
            COMMENT "Checking changed GameWIP C++ files"
            VERBATIM
        )
    else()
        # Keep source paths out of the command line on Windows.
        set(gamewip_format_file_list "${CMAKE_BINARY_DIR}/clang-format-files.txt")
        string(REPLACE ";" "\n" gamewip_format_file_contents "${gamewip_format_files}")
        file(WRITE "${gamewip_format_file_list}" "${gamewip_format_file_contents}\n")

        add_custom_target(
            clang-format-check
            COMMAND "${GAMEWIP_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror "--files=${gamewip_format_file_list}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Checking clang-format for GameWIP-owned C++ sources"
            USES_TERMINAL
            VERBATIM
        )
    endif()

    add_custom_target(static-analysis)
    add_dependencies(static-analysis clang-tidy clang-format-check)
endfunction()
