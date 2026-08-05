include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TEST_SUPPORT_ENABLE_TEST_HOOKS "Enable TestSupport internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/LibraryPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(TestSupport STATIC)
add_library(GameWIP::TestSupport ALIAS TestSupport)

target_sources(TestSupport
    PRIVATE
        core/test_support.cpp
        core/test_support_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}/tools"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/test_support.h"
)

gamewip_target_platform_sources(TestSupport TestSupport "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(TestSupport PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(TestSupport PUBLIC cxx_std_23)
target_compile_definitions(TestSupport
    PRIVATE
        INTERNAL_TEST_SUPPORT_TEST_HOOKS=$<BOOL:${TEST_SUPPORT_ENABLE_TEST_HOOKS}>
    INTERFACE
        $<BUILD_INTERFACE:INTERNAL_TEST_SUPPORT_TEST_HOOKS=$<BOOL:${TEST_SUPPORT_ENABLE_TEST_HOOKS}>>
)

# TestSupport is the validation leaf and must not acquire another GameWIP library dependency.
set(test_support_forbidden_gamewip_targets
    IO FileSystem Terminal Window Logger Assert
    GameWIP::IO GameWIP::FileSystem GameWIP::Terminal GameWIP::Window GameWIP::Logger GameWIP::Assert
)
foreach(property_name LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(property_value TestSupport ${property_name})
    if(property_value)
        foreach(link_item IN LISTS property_value)
            if(link_item IN_LIST test_support_forbidden_gamewip_targets OR link_item MATCHES "^GameWIP::")
                message(FATAL_ERROR "TestSupport must not depend on another GameWIP library, but ${property_name} contains '${link_item}'.")
            endif()
        endforeach()
    endif()
endforeach()

set_target_properties(TestSupport PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # TestSupport owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified library Doxygen site.
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
    EXPORT TestSupportTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestSupportConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TestSupportConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TestSupport
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TestSupportConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(EXPORT TestSupportTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TestSupport
    NAMESPACE GameWIP::
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TestSupportConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TestSupportConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TestSupport
)
