cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

cmake_policy(SET CMP0168 NEW)

get_filename_component(GAMEWIP_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED GAMEWIP_DEPENDENCY_CACHE_DIR OR GAMEWIP_DEPENDENCY_CACHE_DIR STREQUAL "")
    set(GAMEWIP_DEPENDENCY_CACHE_DIR "${GAMEWIP_SOURCE_ROOT}/build/gamewip/cache/dependencies")
endif()

include("${GAMEWIP_SOURCE_ROOT}/cmake/GameWIPDependencies.cmake")

find_package(Git REQUIRED)

set(_cache_root "${GAMEWIP_DEPENDENCY_CACHE_DIR}")
set(_fetchcontent_root "${_cache_root}/fetchcontent")
set(_staging_root "${_cache_root}/staging")
set(_manifest_entries "")

file(MAKE_DIRECTORY "${_fetchcontent_root}")
file(MAKE_DIRECTORY "${_staging_root}")

foreach(gamewip_dependency IN ITEMS tracy benchmark)
    gamewip_get_dependency("${gamewip_dependency}" _repository _commit _cache_directory)

    set(_final_source_directory "${_fetchcontent_root}/${_cache_directory}-src")

    set(_dependency_staging_directory "${_staging_root}/${_cache_directory}")

    set(_staging_source_directory "${_dependency_staging_directory}/source")

    set(_staging_subbuild_directory "${_dependency_staging_directory}/subbuild")

    set(_cached_commit "")

    if(EXISTS "${_final_source_directory}/CMakeLists.txt")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_final_source_directory}" rev-parse HEAD
            RESULT_VARIABLE _git_result
            OUTPUT_VARIABLE _cached_commit
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()

    if(_cached_commit STREQUAL _commit)
        message(STATUS "GameWIP dependency '${gamewip_dependency}' is already cached at " "${_final_source_directory}")
    else()
        message(STATUS "Preparing GameWIP dependency '${gamewip_dependency}' at commit " "${_commit}")

        file(REMOVE_RECURSE "${_dependency_staging_directory}")
        file(MAKE_DIRECTORY "${_dependency_staging_directory}")

        FetchContent_Populate(
            "${gamewip_dependency}"
            GIT_REPOSITORY "${_repository}"
            GIT_TAG "${_commit}"
            SOURCE_DIR "${_staging_source_directory}"
            SUBBUILD_DIR "${_staging_subbuild_directory}"
        )

        if(NOT EXISTS "${_staging_source_directory}/CMakeLists.txt")
            message(FATAL_ERROR "Dependency '${gamewip_dependency}' did not produce a valid " "source directory.")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${_staging_source_directory}" rev-parse HEAD
            RESULT_VARIABLE _git_result
            OUTPUT_VARIABLE _staged_commit
            ERROR_VARIABLE _git_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(NOT _git_result EQUAL 0 OR NOT _staged_commit STREQUAL _commit)
            message(
                FATAL_ERROR
                "Dependency '${gamewip_dependency}' was fetched at an unexpected "
                "commit.\nExpected: ${_commit}\nActual: ${_staged_commit}\n"
                "${_git_error}"
            )
        endif()

        file(REMOVE_RECURSE "${_final_source_directory}")
        file(RENAME "${_staging_source_directory}" "${_final_source_directory}")

        file(REMOVE_RECURSE "${_dependency_staging_directory}")

        message(STATUS "Cached '${gamewip_dependency}' at ${_final_source_directory}")
    endif()

    if(_manifest_entries)
        string(APPEND _manifest_entries ",\n")
    endif()

    string(
        APPEND _manifest_entries
        "    \"${_cache_directory}\": {\n"
        "      \"repository\": \"${_repository}\",\n"
        "      \"commit\": \"${_commit}\",\n"
        "      \"sourceDirectory\": "
        "\"fetchcontent/${_cache_directory}-src\"\n"
        "    }"
    )
endforeach()

file(
    WRITE "${_cache_root}/cache-manifest.json"
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"dependencies\": {\n"
    "${_manifest_entries}\n"
    "  }\n"
    "}\n"
)

message(STATUS "GameWIP dependency cache is ready: ${_cache_root}")
