include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TERMINAL_TEST_HOOKS "Enable Terminal internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(Terminal STATIC)
add_library(GameWIP::Terminal ALIAS Terminal)

target_sources(Terminal PRIVATE
    core/terminal.cpp
    core/terminal_test_hooks.cpp
)

gamewip_target_platform_sources(Terminal Terminal "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(Terminal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Terminal PUBLIC cxx_std_23)
target_compile_definitions(Terminal
    PRIVATE
        GAMEWIP_TERMINAL_TEST_HOOKS=$<BOOL:${TERMINAL_TEST_HOOKS}>
    INTERFACE
        GAMEWIP_TERMINAL_TEST_HOOKS=$<BOOL:${TERMINAL_TEST_HOOKS}>
)

target_link_libraries(Terminal PUBLIC
    GameWIP::IO
)

set_target_properties(Terminal PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # Terminal owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified GameWIP Doxygen site.
    gamewip_register_doxygen_library(
        NAME Terminal
        PAGE_ID foundation_terminal
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/terminal.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS Terminal
    EXPORT GameWIPTerminalTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/terminal.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terminal
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIP_TerminalConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TerminalConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Terminal
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TerminalConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT GameWIPTerminalTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Terminal
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TerminalConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_TerminalConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_Terminal
)
