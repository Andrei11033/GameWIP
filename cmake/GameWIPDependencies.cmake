# Centralizes pinned dependency acquisition and repository-local cache configuration.
# The lock file owns source revisions and package hashes. CMake and setup share
# the same untracked cache. Embedded projects use their build directory by default.
#
# Offline mode consumes only already-populated dependency sources and fails during
# configuration when a required dependency is unavailable.
#
# Shared helper:
# - gamewip_make_dependency_available(<dependency-name>)
# - gamewip_prepare_archive_dependency(<dependency-name> <out-source-directory>)

get_filename_component(GAMEWIP_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(GAMEWIP_DEPENDENCY_LOCK_FILE "${GAMEWIP_SOURCE_ROOT}/scripts/config/dependencies.json" CACHE FILEPATH "GameWIP dependency lock file")

if(PROJECT_IS_TOP_LEVEL)
    set(_gamewip_default_dependency_cache "${GAMEWIP_SOURCE_ROOT}/build/gamewip/cache/dependencies")
else()
    set(_gamewip_default_dependency_cache "")
endif()

set(GAMEWIP_DEPENDENCY_CACHE_DIR "${_gamewip_default_dependency_cache}" CACHE PATH "GameWIP local dependency cache")

unset(_gamewip_default_dependency_cache)

if(GAMEWIP_DEPENDENCY_CACHE_DIR)
    if(IS_ABSOLUTE "${GAMEWIP_DEPENDENCY_CACHE_DIR}")
        set(_gamewip_fetchcontent_root "${GAMEWIP_DEPENDENCY_CACHE_DIR}/fetchcontent")
    else()
        get_filename_component(_gamewip_dependency_cache_root "${GAMEWIP_DEPENDENCY_CACHE_DIR}" ABSOLUTE BASE_DIR "${GAMEWIP_SOURCE_ROOT}")
        set(_gamewip_fetchcontent_root "${_gamewip_dependency_cache_root}/fetchcontent")
    endif()

    # Keep all FetchContent sources in the setup-managed shared cache.
    set(FETCHCONTENT_BASE_DIR "${_gamewip_fetchcontent_root}" CACHE PATH "Shared GameWIP FetchContent cache" FORCE)
else()
    set(_gamewip_fetchcontent_root "")
endif()

include(FetchContent)

if(NOT EXISTS "${GAMEWIP_DEPENDENCY_LOCK_FILE}")
    message(FATAL_ERROR "GameWIP dependency lock file was not found: " "${GAMEWIP_DEPENDENCY_LOCK_FILE}")
endif()

if(GAMEWIP_DEPENDENCIES_OFFLINE)
    find_package(Git REQUIRED)
endif()

function(gamewip_get_dependency DEPENDENCY_NAME OUT_REPOSITORY OUT_COMMIT OUT_CACHE_DIRECTORY)
    file(READ "${GAMEWIP_DEPENDENCY_LOCK_FILE}" _dependency_json)

    string(JSON _repository GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" repository)
    string(JSON _commit GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" commit)
    string(JSON _cache_directory GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" cacheDirectory)

    set("${OUT_REPOSITORY}" "${_repository}" PARENT_SCOPE)
    set("${OUT_COMMIT}" "${_commit}" PARENT_SCOPE)
    set("${OUT_CACHE_DIRECTORY}" "${_cache_directory}" PARENT_SCOPE)
endfunction()

function(gamewip_get_archive_dependency DEPENDENCY_NAME OUT_URL OUT_SHA512 OUT_CACHE_DIRECTORY OUT_ARCHIVE_FILE OUT_VALIDATION_FILE)
    file(READ "${GAMEWIP_DEPENDENCY_LOCK_FILE}" _dependency_json)

    string(JSON _url GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" url)
    string(JSON _sha512 GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" sha512)
    string(JSON _cache_directory GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" cacheDirectory)
    string(JSON _archive_file GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" archiveFile)
    string(JSON _validation_file GET "${_dependency_json}" dependencies "${DEPENDENCY_NAME}" validationFile)

    set("${OUT_URL}" "${_url}" PARENT_SCOPE)
    set("${OUT_SHA512}" "${_sha512}" PARENT_SCOPE)
    set("${OUT_CACHE_DIRECTORY}" "${_cache_directory}" PARENT_SCOPE)
    set("${OUT_ARCHIVE_FILE}" "${_archive_file}" PARENT_SCOPE)
    set("${OUT_VALIDATION_FILE}" "${_validation_file}" PARENT_SCOPE)
endfunction()

function(gamewip_prepare_archive_dependency DEPENDENCY_NAME OUT_SOURCE_DIRECTORY)
    gamewip_get_archive_dependency("${DEPENDENCY_NAME}" _url _sha512 _cache_directory _archive_file _validation_file)

    # Validate locked paths before using them beneath the cache root.
    string(FIND "${_validation_file}" ".." _parent_path_position)
    if(NOT _cache_directory MATCHES "^[A-Za-z0-9._-]+$" OR NOT _archive_file MATCHES "^[A-Za-z0-9._-]+$" OR
       NOT _validation_file MATCHES "^[A-Za-z0-9._/-]+$" OR NOT _parent_path_position EQUAL -1)
        message(FATAL_ERROR "Dependency '${DEPENDENCY_NAME}' has invalid archive cache metadata.")
    endif()

    # Resolve the package and extracted-source locations.
    if(GAMEWIP_DEPENDENCY_CACHE_DIR)
        if(IS_ABSOLUTE "${GAMEWIP_DEPENDENCY_CACHE_DIR}")
            set(_cache_root "${GAMEWIP_DEPENDENCY_CACHE_DIR}")
        else()
            get_filename_component(_cache_root "${GAMEWIP_DEPENDENCY_CACHE_DIR}" ABSOLUTE BASE_DIR "${GAMEWIP_SOURCE_ROOT}")
        endif()
    else()
        set(_cache_root "${CMAKE_BINARY_DIR}/gamewip/cache/dependencies")
    endif()

    set(_source_directory "${_cache_root}/fetchcontent/${_cache_directory}-src")
    set(_archive_directory "${_cache_root}/archives")
    set(_archive_path "${_archive_directory}/${_archive_file}")
    set(_validation_path "${_source_directory}/${_validation_file}")

    # Reuse a verified package or download its locked archive.
    if(EXISTS "${_archive_path}")
        file(SHA512 "${_archive_path}" _actual_sha512)
        string(TOLOWER "${_actual_sha512}" _actual_sha512)
        string(TOLOWER "${_sha512}" _expected_sha512)
        if(NOT _actual_sha512 STREQUAL _expected_sha512)
            message(FATAL_ERROR "Cached archive for '${DEPENDENCY_NAME}' does not match its locked SHA-512.")
        endif()
    else()
        if(GAMEWIP_DEPENDENCIES_OFFLINE)
            message(FATAL_ERROR "Offline dependency '${DEPENDENCY_NAME}' is missing its locked package archive: ${_archive_path}")
        endif()

        file(MAKE_DIRECTORY "${_archive_directory}")
        file(DOWNLOAD
            "${_url}"
            "${_archive_path}"
            EXPECTED_HASH "SHA512=${_sha512}"
            TLS_VERIFY ON
            STATUS _download_status
            SHOW_PROGRESS
        )
        list(GET _download_status 0 _download_code)
        if(NOT _download_code EQUAL 0)
            file(REMOVE "${_archive_path}")
            list(GET _download_status 1 _download_message)
            message(FATAL_ERROR "Could not download '${DEPENDENCY_NAME}': ${_download_message}")
        endif()
    endif()

    # Extract into staging, validate the package, then publish the source tree.
    if(NOT EXISTS "${_validation_path}")
        set(_staging_directory "${_cache_root}/staging/${_cache_directory}-extract")
        file(REMOVE_RECURSE "${_staging_directory}")
        file(MAKE_DIRECTORY "${_staging_directory}")
        file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${_staging_directory}")

        if(NOT EXISTS "${_staging_directory}/${_validation_file}")
            message(FATAL_ERROR "Dependency '${DEPENDENCY_NAME}' does not contain its expected validation file.")
        endif()

        file(REMOVE_RECURSE "${_source_directory}")
        get_filename_component(_source_parent "${_source_directory}" DIRECTORY)
        file(MAKE_DIRECTORY "${_source_parent}")
        file(RENAME "${_staging_directory}" "${_source_directory}")
    endif()

    # Return the verified source location to the caller.
    set("${OUT_SOURCE_DIRECTORY}" "${_source_directory}" PARENT_SCOPE)
endfunction()

function(gamewip_make_dependency_available DEPENDENCY_NAME)
    gamewip_get_dependency("${DEPENDENCY_NAME}" _repository _commit _cache_directory)

    string(TOUPPER "${DEPENDENCY_NAME}" _dependency_upper)
    string(TOLOWER "${DEPENDENCY_NAME}" _dependency_lower)

    if(GAMEWIP_DEPENDENCY_CACHE_DIR)
        set(_expected_source_directory "${_gamewip_fetchcontent_root}/${_cache_directory}-src")
    else()
        set(_expected_source_directory "")
    endif()

    if(GAMEWIP_DEPENDENCIES_OFFLINE)
        if(NOT GAMEWIP_DEPENDENCY_CACHE_DIR)
            message(FATAL_ERROR "Offline GameWIP dependency mode requires " "GAMEWIP_DEPENDENCY_CACHE_DIR to be set.")
        endif()

        if(NOT EXISTS "${_expected_source_directory}/CMakeLists.txt")
            message(
                FATAL_ERROR
                "Offline dependency '${DEPENDENCY_NAME}' is missing from: "
                "${_expected_source_directory}\n"
                "Prepare the cache first with the GameWIP dependency setup command."
            )
        endif()

        set(FETCHCONTENT_SOURCE_DIR_${_dependency_upper} "${_expected_source_directory}")
    endif()

    FetchContent_Declare("${DEPENDENCY_NAME}" GIT_REPOSITORY "${_repository}" GIT_TAG "${_commit}" EXCLUDE_FROM_ALL)

    FetchContent_MakeAvailable("${DEPENDENCY_NAME}")
endfunction()
