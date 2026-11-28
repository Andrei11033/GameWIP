cmake_minimum_required(VERSION "${GAMEWIP_CMAKE_MINIMUM_VERSION}")

cmake_policy(SET CMP0168 NEW)

get_filename_component(GAMEWIP_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${GAMEWIP_SOURCE_ROOT}/cmake/GameWIPStorage.cmake")

if(NOT DEFINED GAMEWIP_DEPENDENCY_CACHE_DIR OR GAMEWIP_DEPENDENCY_CACHE_DIR STREQUAL "")
    gamewip_get_storage_path(_gamewip_cache_root cache)
    set(GAMEWIP_DEPENDENCY_CACHE_DIR "${_gamewip_cache_root}/dependencies")
endif()

include("${GAMEWIP_SOURCE_ROOT}/cmake/GameWIPDependencies.cmake")

find_package(Git REQUIRED)

set(_cache_root "${GAMEWIP_DEPENDENCY_CACHE_DIR}")
set(_fetchcontent_root "${_cache_root}/fetchcontent")
set(_staging_root "${_cache_root}/staging")
set(_manifest_entries "")

file(MAKE_DIRECTORY "${_fetchcontent_root}")
file(MAKE_DIRECTORY "${_staging_root}")

# Prepare pinned Git dependencies in the shared source cache.
foreach(gamewip_dependency IN ITEMS tracy benchmark)
    gamewip_get_dependency("${gamewip_dependency}" _repository _commit _cache_directory)

    set(_final_source_directory "${_fetchcontent_root}/${_cache_directory}-src")
    set(_dependency_staging_directory "${_staging_root}/${_cache_directory}")

    # Keep temporary source and binary output in one removable tree.
    set(_staging_source_directory "${_dependency_staging_directory}/source")
    set(_staging_binary_directory "${_dependency_staging_directory}/binary")

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

    # Reuse a verified checkout or stage a clean fetch before publishing it.
    if(_cached_commit STREQUAL _commit)
        message(STATUS "GameWIP dependency '${gamewip_dependency}' is already cached at " "${_final_source_directory}")
    else()
        message(STATUS "Preparing GameWIP dependency '${gamewip_dependency}' at commit " "${_commit}")

        file(REMOVE_RECURSE "${_dependency_staging_directory}")
        file(MAKE_DIRECTORY "${_dependency_staging_directory}")

        # Stale FetchContent stamps from an interrupted run make it skip the clone and leave nothing to validate.
        file(
            REMOVE_RECURSE
            "${_fetchcontent_root}/${_cache_directory}-stamp"
            "${_fetchcontent_root}/${_cache_directory}-tmp"
            "${_fetchcontent_root}/${_cache_directory}-build"
        )

        if(NOT EXISTS "${_final_source_directory}/CMakeLists.txt")
            file(REMOVE_RECURSE "${_final_source_directory}")
        endif()

        FetchContent_Populate(
            "${gamewip_dependency}"
            GIT_REPOSITORY "${_repository}"
            GIT_TAG "${_commit}"
            SOURCE_DIR "${_staging_source_directory}"
            BINARY_DIR "${_staging_binary_directory}"
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

    # Add the verified checkout to the cache manifest.
    if(_manifest_entries)
        string(APPEND _manifest_entries ",\n")
    endif()

    string(
        APPEND _manifest_entries
        "    \"${gamewip_dependency}\": {\n"
        "      \"repository\": \"${_repository}\",\n"
        "      \"commit\": \"${_commit}\",\n"
        "      \"sourceDirectory\": "
        "\"fetchcontent/${_cache_directory}-src\"\n"
        "    }"
    )
endforeach()

# Prepare the pinned GameInput package archive.
gamewip_get_archive_dependency(
    gameinput
    _gameinput_url
    _gameinput_sha512
    _gameinput_cache_directory
    _gameinput_archive_file
    _gameinput_validation_file
)
gamewip_prepare_archive_dependency(gameinput _gameinput_source_directory)

# Publish cache metadata for the prepared dependencies.
if(_manifest_entries)
    string(APPEND _manifest_entries ",\n")
endif()

string(
    APPEND _manifest_entries
    "    \"gameinput\": {\n"
    "      \"url\": \"${_gameinput_url}\",\n"
    "      \"sha512\": \"${_gameinput_sha512}\",\n"
    "      \"archiveFile\": \"archives/${_gameinput_archive_file}\",\n"
    "      \"sourceDirectory\": \"fetchcontent/${_gameinput_cache_directory}-src\"\n"
    "    }"
)

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
