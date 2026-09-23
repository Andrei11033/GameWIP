# Centralizes pinned dependency acquisition and repository-local cache configuration.
# The lock file owns repository URLs and commits; FetchContent and the setup utility
# share the same untracked cache. Embedded projects receive no cache by default.
#
# Offline mode consumes only already-populated dependency sources and fails during
# configuration when a required dependency is unavailable.
#
# Shared helper:
# - gamewip_make_dependency_available(<dependency-name>)

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
