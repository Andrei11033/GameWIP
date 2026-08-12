option(LOGGER_ENABLE_TEST_HOOKS "Enable Logger internal test hooks" OFF)

include(GenerateExportHeader)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/LibraryPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(Logger SHARED)
add_library(GameWIP::Logger ALIAS Logger)

set(LOGGER_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${LOGGER_GENERATED_INCLUDE_DIR}/logger")
generate_export_header(Logger
    BASE_NAME GAMEWIP_LOGGER
    EXPORT_FILE_NAME "${LOGGER_GENERATED_INCLUDE_DIR}/logger/logger_export.h"
    EXPORT_MACRO_NAME GAMEWIP_LOGGER_EXPORT
    NO_EXPORT_MACRO_NAME GAMEWIP_LOGGER_NO_EXPORT
    STATIC_DEFINE GAMEWIP_LOGGER_STATIC_DEFINE
)

target_sources(Logger
    PRIVATE
        core/logger_filters.cpp
        core/logger_format.cpp
        core/logger_queue.cpp
        core/logger_reports.cpp
        core/logger_state.cpp
        core/logger_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            "${PROJECT_SOURCE_DIR}/tools"
            "${LOGGER_GENERATED_INCLUDE_DIR}"
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/logger.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/logger_macros.h"
            "${LOGGER_GENERATED_INCLUDE_DIR}/logger/logger_export.h"
)

gamewip_target_platform_sources(Logger Logger "${CMAKE_CURRENT_SOURCE_DIR}/platform")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

target_include_directories(Logger PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
    $<BUILD_INTERFACE:${LOGGER_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Logger PUBLIC cxx_std_23)
target_compile_definitions(Logger
    PRIVATE
        INTERNAL_LOGGER_TEST_HOOKS=$<BOOL:${LOGGER_ENABLE_TEST_HOOKS}>
    INTERFACE
        $<BUILD_INTERFACE:INTERNAL_LOGGER_TEST_HOOKS=$<BOOL:${LOGGER_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(Logger
    PUBLIC
        IO
    PRIVATE
        FileSystem
        Terminal
        Unicode
)

set_target_properties(Logger PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # Logger owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified library Doxygen site.
    gamewip_register_doxygen_library(
        NAME Logger
        PAGE_ID logger
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/logger.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/logger_macros.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()


install(TARGETS Logger
    EXPORT LoggerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LoggerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Logger
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(EXPORT LoggerTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Logger
    NAMESPACE GameWIP::
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Logger
)
