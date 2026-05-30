option(LOGGER_TEST_HOOKS "Enable Logger internal test hooks" OFF)

add_library(Logger SHARED)
add_library(GameWIP::Logger ALIAS Logger)

target_sources(Logger PRIVATE
    core/logger.cpp
)

if(WIN32)
    target_sources(Logger PRIVATE
        platform/win32/win32_logger.cpp
    )
    target_link_libraries(Logger PRIVATE
        psapi
    )
else()
    message(FATAL_ERROR "Logger has no platform backend for this operating system yet.")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/logs" LOGGER_DEFAULT_DIRECTORY_PATH)

target_include_directories(Logger PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Logger PUBLIC cxx_std_20)
target_compile_definitions(Logger
    PRIVATE
        LOGGER_BUILD
        LOGGER_DEFAULT_DIRECTORY="${LOGGER_DEFAULT_DIRECTORY_PATH}"
        GAMEWIP_LOGGER_TEST_HOOKS=$<BOOL:${LOGGER_TEST_HOOKS}>
    INTERFACE
        GAMEWIP_LOGGER_TEST_HOOKS=$<BOOL:${LOGGER_TEST_HOOKS}>
)

set_target_properties(Logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # Logger owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified GameWIP Doxygen site.
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
    EXPORT GameWIPLoggerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/logger"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "internal" EXCLUDE
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIP_LoggerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_LoggerConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Logger
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_LoggerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT GameWIPLoggerTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Logger
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_LoggerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_LoggerConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Logger
)
