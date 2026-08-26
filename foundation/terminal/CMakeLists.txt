include(GNUInstallDirs)
include(GenerateExportHeader)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

option(TERMINAL_ENABLE_TEST_HOOKS "Enable Terminal internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(Terminal SHARED)
add_library(GameWIP::Terminal ALIAS Terminal)

set(TERMINAL_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TERMINAL_GENERATED_INCLUDE_DIR}/terminal")
generate_export_header(
    Terminal
    BASE_NAME GAMEWIP_TERMINAL
    EXPORT_FILE_NAME "${TERMINAL_GENERATED_INCLUDE_DIR}/terminal/terminal_export.h"
    EXPORT_MACRO_NAME GAMEWIP_TERMINAL_EXPORT
    NO_EXPORT_MACRO_NAME GAMEWIP_TERMINAL_NO_EXPORT
    STATIC_DEFINE GAMEWIP_TERMINAL_STATIC_DEFINE
)

target_sources(
    Terminal
    PRIVATE core/line_input.cpp core/output_buffer.cpp core/session.cpp core/style.cpp core/terminal.cpp core/terminal_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." "${TERMINAL_GENERATED_INCLUDE_DIR}"
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/style.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/terminal.h"
                "${TERMINAL_GENERATED_INCLUDE_DIR}/terminal/terminal_export.h"
)

gamewip_target_platform_backend(TARGET Terminal ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(
    Terminal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${TERMINAL_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Terminal PUBLIC cxx_std_23)
target_compile_definitions(
    Terminal
    PRIVATE TERMINAL_INTERNAL_TEST_HOOKS=$<BOOL:${TERMINAL_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:TERMINAL_INTERNAL_TEST_HOOKS=$<BOOL:${TERMINAL_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(Terminal PUBLIC IO PRIVATE Unicode $<BUILD_INTERFACE:GameWIPBase>)

set_target_properties(
    Terminal
    PROPERTIES
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
        PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/style.h" "${CMAKE_CURRENT_SOURCE_DIR}/terminal.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(
    TARGETS Terminal
    EXPORT TerminalTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

gamewip_install_package(TARGET Terminal CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TerminalConfig.cmake.in")
