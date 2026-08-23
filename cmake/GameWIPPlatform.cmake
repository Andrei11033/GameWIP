include_guard(GLOBAL)

# Resolves the normalized operating-system backend and includes its target-owned registration file.
#
# Public helpers:
# - gamewip_resolve_platform_id(<output-variable>)
# - gamewip_target_platform_backend(TARGET <target> ROOT <platform-root>)
#
# Inputs:
# - TARGET names an existing target.
# - ROOT names a directory containing <backend>/platform.cmake.
#
# Side effects:
# - Includes exactly the active backend file, which owns sources, libraries, definitions, resources, and private includes.
#
# Failure contract:
# - Unsupported OS families, missing targets, invalid arguments, and absent backend files stop configuration with a descriptive fatal error.

function(gamewip_resolve_platform_id output_variable)
    if(DEFINED GAMEWIP_PLATFORM_ID AND NOT GAMEWIP_PLATFORM_ID STREQUAL "")
        set(${output_variable} "${GAMEWIP_PLATFORM_ID}" PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(gamewip_platform_id win32)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(gamewip_platform_id linux)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(gamewip_platform_id macos)
    else()
        message(FATAL_ERROR "No GameWIP platform backend mapping exists for '${CMAKE_SYSTEM_NAME}'.")
    endif()

    set(GAMEWIP_PLATFORM_ID "${gamewip_platform_id}" CACHE STRING "GameWIP normalized platform backend id")
    set(${output_variable} "${gamewip_platform_id}" PARENT_SCOPE)
endfunction()

function(gamewip_target_platform_backend)
    cmake_parse_arguments(PARSE_ARGV 0 gamewip_backend "" "TARGET;ROOT" "")
    if(gamewip_backend_UNPARSED_ARGUMENTS OR NOT gamewip_backend_TARGET OR NOT gamewip_backend_ROOT)
        message(FATAL_ERROR "gamewip_target_platform_backend requires TARGET <target> ROOT <platform-root>.")
    endif()
    if(NOT TARGET "${gamewip_backend_TARGET}")
        message(FATAL_ERROR "Platform backend target does not exist: ${gamewip_backend_TARGET}")
    endif()

    gamewip_resolve_platform_id(gamewip_backend_id)
    set(gamewip_backend_file "${gamewip_backend_ROOT}/${gamewip_backend_id}/platform.cmake")
    if(NOT EXISTS "${gamewip_backend_file}")
        message(FATAL_ERROR "${gamewip_backend_TARGET} has no '${gamewip_backend_id}' platform backend: ${gamewip_backend_file}")
    endif()

    include("${gamewip_backend_file}")
endfunction()

