include(GNUInstallDirs)
include(GenerateExportHeader)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

option(DESKTOP_ENABLE_TEST_HOOKS "Enable Desktop internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

# Desktop intentionally remains shared-only. Its process-local identities, native-class
# registration, Window registry, and per-thread dispatcher registry must have one coherent
# runtime instance when the library is consumed across executable/DLL boundaries.
add_library(Desktop SHARED)
add_library(GameWIP::Desktop ALIAS Desktop)

set(DESKTOP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DESKTOP_GENERATED_INCLUDE_DIR}/desktop")
generate_export_header(
    Desktop
    BASE_NAME GAMEWIP_DESKTOP
    EXPORT_FILE_NAME "${DESKTOP_GENERATED_INCLUDE_DIR}/desktop/desktop_export.h"
    EXPORT_MACRO_NAME GAMEWIP_DESKTOP_EXPORT
    NO_EXPORT_MACRO_NAME GAMEWIP_DESKTOP_NO_EXPORT
    STATIC_DEFINE GAMEWIP_DESKTOP_STATIC_DEFINE
)

target_sources(
    Desktop
    PRIVATE
        core/clipboard.cpp
        core/child_surface.cpp
        core/child_surface_events.cpp
        core/cursor.cpp
        core/display.cpp
        core/display_info.cpp
        core/drag_drop.cpp
        core/events.cpp
        core/renderer_bridge.cpp
        core/window.cpp
        core/desktop_test_hooks.cpp
        internal/cursor_platform.h
        internal/cursor_selection.h
        internal/cursor_state.h
        internal/drag_drop_platform.h
        internal/drag_drop_state.h
        internal/clipboard_platform.h
        internal/child_surface_platform.h
        internal/child_surface_state.h
        internal/presentation_publication_state.h
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." "${DESKTOP_GENERATED_INCLUDE_DIR}"
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/clipboard.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/child_surface.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/cursor.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/data_transfer.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/description.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/display.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/display_info.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/drag_drop.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/events.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/renderer_bridge.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
                "${DESKTOP_GENERATED_INCLUDE_DIR}/desktop/desktop_export.h"
)

gamewip_target_platform_backend(TARGET Desktop ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(
    Desktop
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${DESKTOP_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Desktop PUBLIC cxx_std_23)
target_compile_definitions(
    Desktop
    PRIVATE DESKTOP_INTERNAL_TEST_HOOKS=$<BOOL:${DESKTOP_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:DESKTOP_INTERNAL_TEST_HOOKS=$<BOOL:${DESKTOP_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(Desktop PUBLIC IO FileSystem PRIVATE Unicode $<BUILD_INTERFACE:GameWIPBase>)

set_target_properties(
    Desktop
    PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Desktop
        PAGE_ID desktop_library
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/clipboard.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/child_surface.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/cursor.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/data_transfer.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/description.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/display.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/display_info.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drag_drop.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/events.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/renderer_bridge.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(
    TARGETS Desktop
    EXPORT DesktopTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

gamewip_install_package(TARGET Desktop CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DesktopConfig.cmake.in" PATH_VARS CMAKE_INSTALL_DATADIR)
