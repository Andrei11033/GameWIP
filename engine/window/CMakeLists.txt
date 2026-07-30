include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)

option(WINDOW_ENABLE_TEST_HOOKS "Enable Window internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_sources)
    include("${PROJECT_SOURCE_DIR}/cmake/LibraryPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

add_library(Window SHARED)
add_library(GameWIP::Window ALIAS Window)

set(WINDOW_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WINDOW_GENERATED_INCLUDE_DIR}/window")
generate_export_header(Window
    BASE_NAME GAMEWIP_WINDOW
    EXPORT_FILE_NAME "${WINDOW_GENERATED_INCLUDE_DIR}/window/window_export.h"
    EXPORT_MACRO_NAME GAMEWIP_WINDOW_EXPORT
    NO_EXPORT_MACRO_NAME GAMEWIP_WINDOW_NO_EXPORT
    STATIC_DEFINE GAMEWIP_WINDOW_STATIC_DEFINE
)

target_sources(Window
    PRIVATE
        core/renderer_feedback.cpp
        core/window.cpp
        core/window_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "${WINDOW_GENERATED_INCLUDE_DIR}"
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/renderer_feedback.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/win32.h"
            "${WINDOW_GENERATED_INCLUDE_DIR}/window/window_export.h"
)

gamewip_target_platform_sources(Window Window "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(Window PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${WINDOW_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Window PUBLIC cxx_std_23)
target_compile_definitions(Window
    PRIVATE
        INTERNAL_WINDOW_TEST_HOOKS=$<BOOL:${WINDOW_ENABLE_TEST_HOOKS}>
    INTERFACE
        $<BUILD_INTERFACE:INTERNAL_WINDOW_TEST_HOOKS=$<BOOL:${WINDOW_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(Window PUBLIC
    IO
    FileSystem
)

if(MINGW)
    # MinGW otherwise auto-exports every cross-translation-unit backend helper. Public and
    # validation-hook ABI entries carry explicit dllexport annotations.
    target_link_options(Window PRIVATE "-Wl,--exclude-all-symbols")
endif()

set_target_properties(Window PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Window
        PAGE_ID window
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/renderer_feedback.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/native/win32.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/internal/window_test_hooks.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS Window
    EXPORT WindowTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WindowConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/WindowConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Window
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/WindowConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(EXPORT WindowTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Window
    NAMESPACE GameWIP::
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/WindowConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/WindowConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Window
)
