option(LOGGER_TEST_HOOKS "Enable Logger internal test hooks" OFF)

if(NOT COMMAND library_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/LibraryPlatform.cmake")
    library_resolve_platform_id(LIBRARY_PLATFORM_ID)
endif()

add_library(Logger SHARED)

target_sources(Logger PRIVATE
    core/logger_filters.cpp
    core/logger_format.cpp
    core/logger_queue.cpp
    core/logger_reports.cpp
    core/logger_state.cpp
    core/logger_test_hooks.cpp
)

library_target_platform_sources(Logger Logger "${CMAKE_CURRENT_SOURCE_DIR}/platform")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/logs" LOGGER_DEFAULT_DIRECTORY_PATH)

target_include_directories(Logger PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Logger PUBLIC cxx_std_23)
target_compile_definitions(Logger
    PRIVATE
        INTERNAL_LOGGER_BUILD
        INTERNAL_LOGGER_DEFAULT_DIRECTORY="${LOGGER_DEFAULT_DIRECTORY_PATH}"
        INTERNAL_LOGGER_TEST_HOOKS=$<BOOL:${LOGGER_TEST_HOOKS}>
    INTERFACE
        INTERNAL_LOGGER_TEST_HOOKS=$<BOOL:${LOGGER_TEST_HOOKS}>
)

set_target_properties(Logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(BUILD_DOCS AND COMMAND library_register_doxygen_library)
    # Logger owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified library Doxygen site.
    library_register_doxygen_library(
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
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/logger.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/logger_macros.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logger
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LoggerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Logger
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT LoggerTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Logger
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LoggerConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Logger
)
