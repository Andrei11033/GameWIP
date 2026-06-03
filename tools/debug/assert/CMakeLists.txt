set(ASSERT_ENABLED "AUTO" CACHE STRING "Force assertions on, off, or let NDEBUG choose")
set_property(CACHE ASSERT_ENABLED PROPERTY STRINGS AUTO ON OFF)
set(ASSERT_CHECKS_ENABLED "AUTO" CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose")
set_property(CACHE ASSERT_CHECKS_ENABLED PROPERTY STRINGS AUTO ON OFF)
option(ASSERT_DIAGNOSTICS "Embed assertion condition/file/function/message diagnostics" ON)
option(ASSERT_UNREACHABLE_ASSUME "Use compiler unreachable assumptions instead of a trap when UNREACHABLE is compiled without assertions" OFF)
option(ASSERT_ENABLE_COMMON_CONTROLS_MANIFEST "Propagate a Common Controls v6 manifest resource to Windows executables using Assert" ON)
option(ASSERT_TEST_HOOKS "Enable Assert internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

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

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertOptions.cmake")

gamewip_assert_resolve_auto_option(ASSERT_ENABLED_EFFECTIVE "${ASSERT_ENABLED_VALUE}")
gamewip_assert_resolve_auto_option(ASSERT_CHECKS_ENABLED_EFFECTIVE "${ASSERT_CHECKS_ENABLED_VALUE}")
gamewip_assert_public_option(ASSERT_ENABLED_PUBLIC "${ASSERT_ENABLED_VALUE}")
gamewip_assert_public_option(ASSERT_CHECKS_ENABLED_PUBLIC "${ASSERT_CHECKS_ENABLED_VALUE}")

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

    gamewip_target_platform_sources(Assert Assert "${CMAKE_CURRENT_SOURCE_DIR}/platform")
else()
    gamewip_include_platform_cmake_if_present("${CMAKE_CURRENT_SOURCE_DIR}/platform")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
            GAMEWIP_ASSERT_TEST_HOOKS=$<BOOL:${ASSERT_TEST_HOOKS}>
        INTERFACE
            GAMEWIP_ASSERT_RUNTIME=${ASSERT_RUNTIME_PUBLIC}
            GAMEWIP_ASSERT_ENABLED=${ASSERT_ENABLED_PUBLIC}
            GAMEWIP_ASSERT_CHECKS_ENABLED=${ASSERT_CHECKS_ENABLED_PUBLIC}
            GAMEWIP_ASSERT_TEST_HOOKS=$<BOOL:${ASSERT_TEST_HOOKS}>
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
        GAMEWIP_ASSERT_TEST_HOOKS=0
    )
endif()

target_compile_definitions(Assert INTERFACE
    GAMEWIP_ASSERT_DIAGNOSTICS=${ASSERT_DIAGNOSTICS_VALUE}
    GAMEWIP_ASSERT_UNREACHABLE_ASSUME=${ASSERT_UNREACHABLE_ASSUME_VALUE}
)

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_link_libraries(Assert PRIVATE
        Logger
    )
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    set_target_properties(Assert PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # Assert owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified GameWIP Doxygen site.
    gamewip_register_doxygen_library(
        NAME Assert
        PAGE_ID assert
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/assert.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
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

install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/common"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

if(GAMEWIP_PLATFORM_ID STREQUAL "win32")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_controls_v6.manifest"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common_controls_v6.rc.in"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/GameWIP_Assert
    )
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIP_AssertConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_AssertConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Assert
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_AssertConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT GameWIPAssertTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Assert
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_AssertConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_AssertConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Assert
)
