include_guard(GLOBAL)

include(CMakeParseArguments)

function(gamewip_compose_display_version output_variable)
    set(options RELEASE DIRTY)
    set(one_value_arguments PROJECT_VERSION BUILD_NUMBER GIT_COMMIT)
    cmake_parse_arguments(VERSION "${options}" "${one_value_arguments}" "" ${ARGN})

    foreach(required_argument IN ITEMS PROJECT_VERSION BUILD_NUMBER GIT_COMMIT)
        if(NOT DEFINED VERSION_${required_argument} OR VERSION_${required_argument} STREQUAL "")
            message(FATAL_ERROR "gamewip_compose_display_version requires ${required_argument}.")
        endif()
    endforeach()

    if(VERSION_RELEASE AND NOT VERSION_DIRTY)
        set(display_version "${VERSION_PROJECT_VERSION}")
    else()
        if(VERSION_BUILD_NUMBER MATCHES "^[0-9]+$")
            set(display_version "${VERSION_PROJECT_VERSION}-dev.${VERSION_BUILD_NUMBER}")
        else()
            set(display_version "${VERSION_PROJECT_VERSION}-dev")
        endif()

        set(display_version "${display_version}+g${VERSION_GIT_COMMIT}")
        if(VERSION_DIRTY)
            string(APPEND display_version ".dirty")
        endif()
    endif()

    set(${output_variable} "${display_version}" PARENT_SCOPE)
endfunction()

function(gamewip_detect_version output_prefix)
    set(options DISABLE_GIT)
    set(one_value_arguments SOURCE_DIR PROJECT_VERSION)
    cmake_parse_arguments(VERSION "${options}" "${one_value_arguments}" "" ${ARGN})

    foreach(required_argument IN ITEMS SOURCE_DIR PROJECT_VERSION)
        if(NOT DEFINED VERSION_${required_argument} OR VERSION_${required_argument} STREQUAL "")
            message(FATAL_ERROR "gamewip_detect_version requires ${required_argument}.")
        endif()
    endforeach()

    set(build_number "unknown")
    set(git_commit "unknown")
    set(is_dirty FALSE)
    set(is_release FALSE)

    if(NOT VERSION_DISABLE_GIT)
        find_package(Git QUIET)
    endif()
    if(Git_FOUND AND NOT VERSION_DISABLE_GIT)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${VERSION_SOURCE_DIR}" rev-parse --is-inside-work-tree
            RESULT_VARIABLE inside_work_tree_result
            OUTPUT_VARIABLE inside_work_tree
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(inside_work_tree_result EQUAL 0 AND inside_work_tree STREQUAL "true")
            set(is_dirty TRUE)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${VERSION_SOURCE_DIR}" rev-list --first-parent --count HEAD
                RESULT_VARIABLE build_number_result
                OUTPUT_VARIABLE detected_build_number
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(build_number_result EQUAL 0 AND detected_build_number MATCHES "^[0-9]+$")
                set(build_number "${detected_build_number}")
            endif()

            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${VERSION_SOURCE_DIR}" rev-parse --short=8 HEAD
                RESULT_VARIABLE git_commit_result
                OUTPUT_VARIABLE detected_git_commit
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(git_commit_result EQUAL 0 AND detected_git_commit MATCHES "^[0-9A-Fa-f]+$")
                string(TOLOWER "${detected_git_commit}" git_commit)
            endif()

            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${VERSION_SOURCE_DIR}" status --porcelain --untracked-files=no
                RESULT_VARIABLE dirty_result
                OUTPUT_VARIABLE dirty_output
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(dirty_result EQUAL 0 AND dirty_output STREQUAL "")
                set(is_dirty FALSE)
            endif()

            set(expected_tag "v${VERSION_PROJECT_VERSION}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${VERSION_SOURCE_DIR}" tag --points-at HEAD
                RESULT_VARIABLE tags_result
                OUTPUT_VARIABLE tags_at_head
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(tags_result EQUAL 0)
                string(REPLACE "\n" ";" tags_at_head "${tags_at_head}")
                if(expected_tag IN_LIST tags_at_head)
                    execute_process(
                        COMMAND "${GIT_EXECUTABLE}" -C "${VERSION_SOURCE_DIR}" cat-file -t "refs/tags/${expected_tag}"
                        RESULT_VARIABLE tag_type_result
                        OUTPUT_VARIABLE tag_type
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                    )
                    if(tag_type_result EQUAL 0 AND tag_type STREQUAL "tag")
                        set(is_release TRUE)
                    endif()
                endif()
            endif()
        endif()
    endif()

    if(is_dirty)
        set(is_release FALSE)
    endif()

    set(compose_options)
    if(is_release)
        list(APPEND compose_options RELEASE)
    endif()
    if(is_dirty)
        list(APPEND compose_options DIRTY)
    endif()
    gamewip_compose_display_version(
        display_version
        PROJECT_VERSION "${VERSION_PROJECT_VERSION}"
        BUILD_NUMBER "${build_number}"
        GIT_COMMIT "${git_commit}"
        ${compose_options}
    )

    set(${output_prefix}_NUMBER "${VERSION_PROJECT_VERSION}" PARENT_SCOPE)
    set(${output_prefix}_DISPLAY "${display_version}" PARENT_SCOPE)
    set(${output_prefix}_BUILD_NUMBER "${build_number}" PARENT_SCOPE)
    set(${output_prefix}_GIT_COMMIT "${git_commit}" PARENT_SCOPE)
    set(${output_prefix}_DIRTY "${is_dirty}" PARENT_SCOPE)
    set(${output_prefix}_RELEASE "${is_release}" PARENT_SCOPE)
endfunction()

function(gamewip_write_version_header output_prefix)
    set(one_value_arguments SOURCE_DIR BINARY_DIR PROJECT_VERSION)
    cmake_parse_arguments(VERSION "" "${one_value_arguments}" "" ${ARGN})

    foreach(required_argument IN ITEMS SOURCE_DIR BINARY_DIR PROJECT_VERSION)
        if(NOT DEFINED VERSION_${required_argument} OR VERSION_${required_argument} STREQUAL "")
            message(FATAL_ERROR "gamewip_write_version_header requires ${required_argument}.")
        endif()
    endforeach()

    gamewip_detect_version(
        detected
        SOURCE_DIR "${VERSION_SOURCE_DIR}"
        PROJECT_VERSION "${VERSION_PROJECT_VERSION}"
    )

    if(detected_DIRTY)
        set(version_dirty_cpp true)
    else()
        set(version_dirty_cpp false)
    endif()
    if(detected_RELEASE)
        set(version_release_cpp true)
    else()
        set(version_release_cpp false)
    endif()

    set(GAMEWIP_VERSION_NUMBER "${detected_NUMBER}")
    set(GAMEWIP_VERSION_DISPLAY "${detected_DISPLAY}")
    set(GAMEWIP_VERSION_BUILD_NUMBER "${detected_BUILD_NUMBER}")
    set(GAMEWIP_VERSION_GIT_COMMIT "${detected_GIT_COMMIT}")
    set(GAMEWIP_VERSION_DIRTY_CPP "${version_dirty_cpp}")
    set(GAMEWIP_VERSION_RELEASE_CPP "${version_release_cpp}")
    file(MAKE_DIRECTORY "${VERSION_BINARY_DIR}/generated/gamewip")
    configure_file(
        "${VERSION_SOURCE_DIR}/game/runtime/version.h.in"
        "${VERSION_BINARY_DIR}/generated/gamewip/version.h"
        @ONLY
    )

    foreach(component IN ITEMS NUMBER DISPLAY BUILD_NUMBER GIT_COMMIT DIRTY RELEASE)
        set(${output_prefix}_${component} "${detected_${component}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(gamewip_configure_version)
    gamewip_write_version_header(
        configured
        SOURCE_DIR "${PROJECT_SOURCE_DIR}"
        BINARY_DIR "${PROJECT_BINARY_DIR}"
        PROJECT_VERSION "${PROJECT_VERSION}"
    )

    foreach(component IN ITEMS NUMBER DISPLAY BUILD_NUMBER GIT_COMMIT DIRTY RELEASE)
        set(GAMEWIP_VERSION_${component} "${configured_${component}}" PARENT_SCOPE)
    endforeach()

    message(STATUS "GameWIP version: ${configured_DISPLAY}")
endfunction()
