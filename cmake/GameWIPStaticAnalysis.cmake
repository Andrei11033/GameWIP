function(gamewip_create_static_analysis_targets)
    if(NOT GAMEWIP_ENABLE_STATIC_ANALYSIS)
        return()
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(GAMEWIP_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    find_program(GAMEWIP_RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py REQUIRED)
    find_program(GAMEWIP_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)

    set(gamewip_owned_source_regex ".*[\\\\/](foundation|tools|engine|game)[\\\\/].*[.](c|cc|cpp|cxx)$")
    set(gamewip_owned_header_regex ".*[\\\\/](foundation|tools|engine|game)[\\\\/].*")
    set(gamewip_excluded_header_regex ".*[\\\\/](external|build-[^\\\\/]+)[\\\\/].*")

    add_custom_target(clang-tidy
        COMMAND "${Python3_EXECUTABLE}" "${GAMEWIP_RUN_CLANG_TIDY_EXECUTABLE}"
            -p "${CMAKE_BINARY_DIR}"
            -clang-tidy-binary "${GAMEWIP_CLANG_TIDY_EXECUTABLE}"
            -config-file "${PROJECT_SOURCE_DIR}/.clang-tidy"
            -source-filter "${gamewip_owned_source_regex}"
            -header-filter "${gamewip_owned_header_regex}"
            -exclude-header-filter "${gamewip_excluded_header_regex}"
            -j "${GAMEWIP_CLANG_TIDY_JOBS}"
            -quiet
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Running clang-tidy on GameWIP-owned C++ sources"
        VERBATIM
    )

    file(GLOB_RECURSE gamewip_format_files CONFIGURE_DEPENDS
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

    add_custom_target(clang-format-check
        COMMAND "${GAMEWIP_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${gamewip_format_files}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking clang-format for GameWIP-owned C++ sources"
        VERBATIM
    )

    add_custom_target(static-analysis)
    add_dependencies(static-analysis clang-tidy clang-format-check)
endfunction()
