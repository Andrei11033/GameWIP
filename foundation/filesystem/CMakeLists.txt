include(GNUInstallDirs)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

option(FILESYSTEM_ENABLE_TEST_HOOKS "Enable FileSystem internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(FileSystem STATIC)
add_library(GameWIP::FileSystem ALIAS FileSystem)

target_sources(
    FileSystem
    PRIVATE core/filesystem.cpp core/path.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES "${CMAKE_CURRENT_SOURCE_DIR}/filesystem.h"
)

gamewip_target_platform_backend(TARGET FileSystem ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(FileSystem PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(FileSystem PUBLIC cxx_std_23)
target_compile_definitions(
    FileSystem
    PRIVATE FILESYSTEM_INTERNAL_TEST_HOOKS=$<BOOL:${FILESYSTEM_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:FILESYSTEM_INTERNAL_TEST_HOOKS=$<BOOL:${FILESYSTEM_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(FileSystem PUBLIC IO PRIVATE Unicode $<BUILD_INTERFACE:GameWIPBase>)

set_target_properties(FileSystem PROPERTIES POSITION_INDEPENDENT_CODE ON ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME FileSystem
        PAGE_ID filesystem
        PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/filesystem.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(
    TARGETS FileSystem
    EXPORT FileSystemTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

gamewip_install_package(TARGET FileSystem CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FileSystemConfig.cmake.in")
