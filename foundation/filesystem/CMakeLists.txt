include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(FILESYSTEM_ENABLE_TEST_HOOKS "Enable FileSystem internal test hooks" OFF)

if(NOT COMMAND library_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/LibraryPlatform.cmake")
    library_resolve_platform_id(LIBRARY_PLATFORM_ID)
endif()

add_library(FileSystem STATIC)
add_library(GameWIP::FileSystem ALIAS FileSystem)

target_sources(FileSystem
    PRIVATE
        core/filesystem.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/filesystem.h"
)

library_target_platform_sources(FileSystem FileSystem "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(FileSystem PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(FileSystem PUBLIC cxx_std_23)
target_compile_definitions(FileSystem
    PRIVATE
        INTERNAL_FILESYSTEM_TEST_HOOKS=$<BOOL:${FILESYSTEM_ENABLE_TEST_HOOKS}>
    INTERFACE
        $<BUILD_INTERFACE:INTERNAL_FILESYSTEM_TEST_HOOKS=$<BOOL:${FILESYSTEM_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(FileSystem PUBLIC
    IO
)

set_target_properties(FileSystem PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND library_register_doxygen_library)
    library_register_doxygen_library(
        NAME FileSystem
        PAGE_ID foundation_filesystem
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/filesystem.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS FileSystem
    EXPORT FileSystemTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FileSystemConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FileSystemConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FileSystem
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FileSystemConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(EXPORT FileSystemTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FileSystem
    NAMESPACE GameWIP::
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FileSystemConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FileSystemConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FileSystem
)
