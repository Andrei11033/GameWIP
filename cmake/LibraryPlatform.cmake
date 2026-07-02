include_guard(GLOBAL)

function(gamewip_resolve_platform_id output_variable)
    if(DEFINED GAMEWIP_PLATFORM_ID AND NOT GAMEWIP_PLATFORM_ID STREQUAL "")
        set(${output_variable} "${GAMEWIP_PLATFORM_ID}" PARENT_SCOPE)
        return()
    endif()

    if(WIN32)
        set(resolved_platform_id win32)
    elseif(APPLE)
        set(resolved_platform_id macos)
    elseif(UNIX)
        set(resolved_platform_id linux)
    else()
        message(FATAL_ERROR "No library platform backend id is defined for this CMake platform.")
    endif()

    set(GAMEWIP_PLATFORM_ID "${resolved_platform_id}" CACHE STRING "GameWIP library platform backend id")
    set(${output_variable} "${resolved_platform_id}" PARENT_SCOPE)
endfunction()

function(gamewip_target_platform_sources target_name library_name platform_root)
    if(NOT GAMEWIP_PLATFORM_ID)
        message(FATAL_ERROR "GAMEWIP_PLATFORM_ID is not set before configuring ${library_name}.")
    endif()

    set(platform_dir "${platform_root}/${GAMEWIP_PLATFORM_ID}")
    if(NOT IS_DIRECTORY "${platform_dir}")
        message(FATAL_ERROR "${library_name} has no platform backend directory for '${GAMEWIP_PLATFORM_ID}': ${platform_dir}")
    endif()

    file(GLOB platform_sources CONFIGURE_DEPENDS
        "${platform_dir}/*.cpp"
    )

    if(NOT platform_sources)
        message(FATAL_ERROR "${library_name} has no platform backend sources for '${GAMEWIP_PLATFORM_ID}' in ${platform_dir}.")
    endif()

    target_sources("${target_name}" PRIVATE
        ${platform_sources}
    )

    set(platform_cmake "${platform_dir}/platform.cmake")
    if(EXISTS "${platform_cmake}")
        include("${platform_cmake}")
    endif()
endfunction()

function(gamewip_include_platform_cmake_if_present platform_root)
    if(NOT GAMEWIP_PLATFORM_ID)
        message(FATAL_ERROR "GAMEWIP_PLATFORM_ID is not set before including platform CMake.")
    endif()

    set(platform_cmake "${platform_root}/${GAMEWIP_PLATFORM_ID}/platform.cmake")
    if(EXISTS "${platform_cmake}")
        include("${platform_cmake}")
    endif()
endfunction()
