set(ASSERT_ENABLED "AUTO" CACHE STRING "Force assertions on, off, or let NDEBUG choose")
set_property(CACHE ASSERT_ENABLED PROPERTY STRINGS AUTO ON OFF)
set(ASSERT_CHECKS_ENABLED "AUTO" CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose")
set_property(CACHE ASSERT_CHECKS_ENABLED PROPERTY STRINGS AUTO ON OFF)
option(ASSERT_DIAGNOSTICS "Embed assertion condition/file/function/message diagnostics" ON)
option(ASSERT_UNREACHABLE_ASSUME "Use compiler unreachable assumptions instead of a trap when UNREACHABLE is compiled without assertions" OFF)
option(ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST "Propagate a Common Controls v6 manifest resource to Windows executables using Assert" ON)

string(TOUPPER "${ASSERT_ENABLED}" ASSERT_ENABLED_VALUE)
string(TOUPPER "${ASSERT_CHECKS_ENABLED}" ASSERT_CHECKS_ENABLED_VALUE)
string(TOUPPER "${CMAKE_BUILD_TYPE}" ASSERT_BUILD_TYPE_VALUE)

if(NOT ASSERT_ENABLED_VALUE STREQUAL "AUTO" AND NOT ASSERT_ENABLED_VALUE STREQUAL "ON" AND NOT ASSERT_ENABLED_VALUE STREQUAL "OFF")
    message(FATAL_ERROR "ASSERT_ENABLED must be AUTO, ON, or OFF.")
endif()

if(NOT ASSERT_CHECKS_ENABLED_VALUE STREQUAL "AUTO" AND NOT ASSERT_CHECKS_ENABLED_VALUE STREQUAL "ON" AND NOT ASSERT_CHECKS_ENABLED_VALUE STREQUAL "OFF")
    message(FATAL_ERROR "ASSERT_CHECKS_ENABLED must be AUTO, ON, or OFF.")
endif()

set(ASSERT_RELEASE_CONFIG_EXPRESSION "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>")
set(ASSERT_MULTI_CONFIG OFF)
if(CMAKE_CONFIGURATION_TYPES)
    set(ASSERT_MULTI_CONFIG ON)
endif()

function(assert_resolve_auto_option output_variable option_value)
    if(option_value STREQUAL "ON")
        set(${output_variable} 1 PARENT_SCOPE)
    elseif(option_value STREQUAL "OFF")
        set(${output_variable} 0 PARENT_SCOPE)
    elseif(ASSERT_BUILD_TYPE_VALUE STREQUAL "RELEASE" OR ASSERT_BUILD_TYPE_VALUE STREQUAL "RELWITHDEBINFO" OR ASSERT_BUILD_TYPE_VALUE STREQUAL "MINSIZEREL")
        set(${output_variable} 0 PARENT_SCOPE)
    else()
        set(${output_variable} 1 PARENT_SCOPE)
    endif()
endfunction()

function(assert_public_option output_variable option_value)
    if(option_value STREQUAL "ON")
        set(${output_variable} 1 PARENT_SCOPE)
    elseif(option_value STREQUAL "OFF")
        set(${output_variable} 0 PARENT_SCOPE)
    elseif(ASSERT_MULTI_CONFIG)
        set(${output_variable} "$<IF:${ASSERT_RELEASE_CONFIG_EXPRESSION},0,1>" PARENT_SCOPE)
    else()
        assert_resolve_auto_option(resolved_value "${option_value}")
        set(${output_variable} ${resolved_value} PARENT_SCOPE)
    endif()
endfunction()

assert_resolve_auto_option(ASSERT_ENABLED_EFFECTIVE "${ASSERT_ENABLED_VALUE}")
assert_resolve_auto_option(ASSERT_CHECKS_ENABLED_EFFECTIVE "${ASSERT_CHECKS_ENABLED_VALUE}")
assert_public_option(ASSERT_ENABLED_PUBLIC "${ASSERT_ENABLED_VALUE}")
assert_public_option(ASSERT_CHECKS_ENABLED_PUBLIC "${ASSERT_CHECKS_ENABLED_VALUE}")

set(ASSERT_RUNTIME_TARGET_ENABLED OFF)
if(ASSERT_MULTI_CONFIG)
    if(NOT (ASSERT_ENABLED_VALUE STREQUAL "OFF" AND ASSERT_CHECKS_ENABLED_VALUE STREQUAL "OFF"))
        set(ASSERT_RUNTIME_TARGET_ENABLED ON)
    endif()
else()
    if(ASSERT_ENABLED_EFFECTIVE OR ASSERT_CHECKS_ENABLED_EFFECTIVE)
        set(ASSERT_RUNTIME_TARGET_ENABLED ON)
    endif()
endif()

if(ASSERT_ENABLED_VALUE STREQUAL "ON" OR ASSERT_CHECKS_ENABLED_VALUE STREQUAL "ON")
    set(ASSERT_RUNTIME_PUBLIC 1)
elseif(ASSERT_ENABLED_VALUE STREQUAL "OFF" AND ASSERT_CHECKS_ENABLED_VALUE STREQUAL "OFF")
    set(ASSERT_RUNTIME_PUBLIC 0)
elseif(ASSERT_MULTI_CONFIG)
    set(ASSERT_RUNTIME_PUBLIC "$<IF:${ASSERT_RELEASE_CONFIG_EXPRESSION},0,1>")
elseif(ASSERT_ENABLED_EFFECTIVE OR ASSERT_CHECKS_ENABLED_EFFECTIVE)
    set(ASSERT_RUNTIME_PUBLIC 1)
else()
    set(ASSERT_RUNTIME_PUBLIC 0)
endif()

if(ASSERT_DIAGNOSTICS)
    set(ASSERT_DIAGNOSTICS_VALUE 1)
else()
    set(ASSERT_DIAGNOSTICS_VALUE 0)
endif()

if(ASSERT_UNREACHABLE_ASSUME)
    set(ASSERT_UNREACHABLE_ASSUME_VALUE 1)
else()
    set(ASSERT_UNREACHABLE_ASSUME_VALUE 0)
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    add_library(Assert SHARED)
else()
    add_library(Assert INTERFACE)
endif()
add_library(GameWIP::Assert ALIAS Assert)

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_sources(Assert PRIVATE
        core/assert.cpp
    )

    if(WIN32)
        target_sources(Assert PRIVATE
            platform/win32/win32_assert.cpp
        )
    else()
        message(FATAL_ERROR "Assert has no platform backend for this operating system yet.")
    endif()
endif()

include(GNUInstallDirs)

set(GAMEWIP_ASSERT_COMMON_CONTROLS_RC "")
if(WIN32 AND ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST)
    enable_language(RC)

    set(
        GAMEWIP_ASSERT_COMMON_CONTROLS_MANIFEST
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_controls_v6.manifest"
    )

    set(
        GAMEWIP_ASSERT_COMMON_CONTROLS_RC
        "${CMAKE_CURRENT_BINARY_DIR}/common_controls_v6.rc"
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_controls_v6.rc.in"
        "${GAMEWIP_ASSERT_COMMON_CONTROLS_RC}"
        @ONLY
    )

    set_property(
        GLOBAL PROPERTY
        GAMEWIP_ASSERT_COMMON_CONTROLS_RC
        "${GAMEWIP_ASSERT_COMMON_CONTROLS_RC}"
    )
endif()

function(gamewip_assert_enable_common_controls_v6 target_name)
    if(NOT WIN32)
        return()
    endif()

    if(NOT ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST)
        message(FATAL_ERROR "ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST is OFF, so no Common Controls v6 resource was configured.")
    endif()

    get_property(
        common_controls_rc
        GLOBAL PROPERTY GAMEWIP_ASSERT_COMMON_CONTROLS_RC
    )

    if(NOT common_controls_rc)
        message(FATAL_ERROR "GameWIP Assert Common Controls v6 resource was not configured.")
    endif()

    target_sources("${target_name}" PRIVATE
        "${common_controls_rc}"
    )
endfunction()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_include_directories(Assert PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(Assert PUBLIC cxx_std_20)
    target_compile_definitions(Assert
        PRIVATE
            GAMEWIP_ASSERT_BUILD
            GAMEWIP_ASSERT_RUNTIME=1
            GAMEWIP_ASSERT_ENABLED=1
            GAMEWIP_ASSERT_CHECKS_ENABLED=1
            GAMEWIP_ASSERT_DIAGNOSTICS=${ASSERT_DIAGNOSTICS_VALUE}
            GAMEWIP_ASSERT_UNREACHABLE_ASSUME=${ASSERT_UNREACHABLE_ASSUME_VALUE}
        INTERFACE
            GAMEWIP_ASSERT_RUNTIME=${ASSERT_RUNTIME_PUBLIC}
            GAMEWIP_ASSERT_ENABLED=${ASSERT_ENABLED_PUBLIC}
            GAMEWIP_ASSERT_CHECKS_ENABLED=${ASSERT_CHECKS_ENABLED_PUBLIC}
    )
else()
    target_include_directories(Assert INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(Assert INTERFACE cxx_std_20)
    target_compile_definitions(Assert INTERFACE
        GAMEWIP_ASSERT_RUNTIME=0
        GAMEWIP_ASSERT_ENABLED=0
        GAMEWIP_ASSERT_CHECKS_ENABLED=0
    )
endif()

target_compile_definitions(Assert INTERFACE
    GAMEWIP_ASSERT_DIAGNOSTICS=${ASSERT_DIAGNOSTICS_VALUE}
    GAMEWIP_ASSERT_UNREACHABLE_ASSUME=${ASSERT_UNREACHABLE_ASSUME_VALUE}
)

if(WIN32 AND ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST)
    target_sources(Assert INTERFACE
        "$<BUILD_INTERFACE:${GAMEWIP_ASSERT_COMMON_CONTROLS_RC}>"
    )
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_link_libraries(Assert PRIVATE
        Logger
    )

    if(WIN32)
        target_link_libraries(Assert PRIVATE
            comctl32
        )
    endif()
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    set_target_properties(Assert PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    install(TARGETS Assert
        EXPORT GameWIPAssertTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
else()
    install(TARGETS Assert
        EXPORT GameWIPAssertTargets
    )
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/debug/assert"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/debug
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "internal" EXCLUDE
)

install(EXPORT GameWIPAssertTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIPAssert
)
