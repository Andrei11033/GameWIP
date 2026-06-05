include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(TestSupport STATIC)

add_library(GameWIP::TestSupport ALIAS TestSupport)

target_sources(TestSupport PRIVATE
    core/test_support.cpp
)

gamewip_target_platform_sources(TestSupport TestSupport "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(TestSupport PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(TestSupport PUBLIC cxx_std_23)

set_target_properties(TestSupport PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # TestSupport owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified GameWIP Doxygen site.
    gamewip_register_doxygen_library(
        NAME TestSupport
        PAGE_ID test_support
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/test_support.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS TestSupport
    EXPORT GameWIPTestSupportTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/test_support"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "internal" EXCLUDE
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIP_TestSupportConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TestSupportConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_TestSupport
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TestSupportConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT GameWIPTestSupportTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_TestSupport
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TestSupportConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TestSupportConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_TestSupport
)
