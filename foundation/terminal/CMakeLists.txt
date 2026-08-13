include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

option(TERMINAL_ENABLE_TEST_HOOKS "Enable Terminal internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/LibraryPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(Terminal SHARED)
add_library(GameWIP::Terminal ALIAS Terminal)

set(TERMINAL_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TERMINAL_GENERATED_INCLUDE_DIR}/terminal")
generate_export_header(Terminal
    BASE_NAME GAMEWIP_TERMINAL
    EXPORT_FILE_NAME "${TERMINAL_GENERATED_INCLUDE_DIR}/terminal/terminal_export.h"
    EXPORT_MACRO_NAME GAMEWIP_TERMINAL_EXPORT
    NO_EXPORT_MACRO_NAME GAMEWIP_TERMINAL_NO_EXPORT
    STATIC_DEFINE GAMEWIP_TERMINAL_STATIC_DEFINE
)

target_sources(Terminal
    PRIVATE
        core/line_input.cpp
        core/output_buffer.cpp
        core/session.cpp
        core/style.cpp
        core/terminal.cpp
        core/terminal_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "${TERMINAL_GENERATED_INCLUDE_DIR}"
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/style.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/terminal.h"
            "${TERMINAL_GENERATED_INCLUDE_DIR}/terminal/terminal_export.h"
)

gamewip_target_platform_sources(Terminal Terminal "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(Terminal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${TERMINAL_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Terminal PUBLIC cxx_std_23)
target_compile_definitions(Terminal
    PRIVATE
        TERMINAL_INTERNAL_TEST_HOOKS=$<BOOL:${TERMINAL_ENABLE_TEST_HOOKS}>
    INTERFACE
        $<BUILD_INTERFACE:TERMINAL_INTERNAL_TEST_HOOKS=$<BOOL:${TERMINAL_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(Terminal
    PUBLIC
        IO
    PRIVATE
        Unicode
)

set_target_properties(Terminal PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # Terminal owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified library Doxygen site.
    gamewip_register_doxygen_library(
        NAME Terminal
        PAGE_ID terminal
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/style.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/terminal.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS Terminal
    EXPORT TerminalTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TerminalConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TerminalConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Terminal
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TerminalConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(EXPORT TerminalTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Terminal
    NAMESPACE GameWIP::
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TerminalConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TerminalConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Terminal
)
