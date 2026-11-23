# Desktop is a shared runtime so process-local native class registration,
# Window identity, and dispatcher state remain coherent across module boundaries.
include(GNUInstallDirs)
include("${PROJECT_SOURCE_DIR}/cmake/GameWIPVisibility.cmake")

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

# ------------------------------------------------------------
# Configuration, generated exports, and target sources
# ------------------------------------------------------------

option(DESKTOP_ENABLE_TEST_HOOKS "Enable Desktop internal test hooks" OFF)

if(NOT COMMAND gamewip_add_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

# ------------------------------------------------------------
# Target and source ownership
# ------------------------------------------------------------

add_library(Desktop SHARED)
add_library(GameWIP::Desktop ALIAS Desktop)
add_subdirectory(accessibility)

set(DESKTOP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${DESKTOP_GENERATED_INCLUDE_DIR}/desktop")
# Keep installed headers and generated API documentation in sync.
set(DESKTOP_PUBLIC_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/accessibility.h"
    ${DESKTOP_ACCESSIBILITY_PUBLIC_HEADERS}
    "${CMAKE_CURRENT_SOURCE_DIR}/child_surface.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/clipboard.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/cursor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/data_transfer.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/description.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/dialogs.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/display.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/display_info.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/drag_drop.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/events.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer_bridge.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/result.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_jump_lists.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_notifications.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_registration.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_taskbar.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_tray.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_types.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
)

gamewip_generate_export_header(
    # DESKTOP_EXPORT is the installed production/runtime ABI. Source-tree
    # validation hooks use desktop_test_export.h and are never installed.
    TARGET Desktop
    OUTPUT "${DESKTOP_GENERATED_INCLUDE_DIR}/desktop/desktop_export.h"
    EXPORT_MACRO DESKTOP_EXPORT
    BUILD_DEFINE DESKTOP_BUILDING_DLL
)

target_sources(
    Desktop
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." "${DESKTOP_GENERATED_INCLUDE_DIR}"
            FILES ${DESKTOP_PUBLIC_HEADERS} "${DESKTOP_GENERATED_INCLUDE_DIR}/desktop/desktop_export.h"
)

add_subdirectory(core)
add_subdirectory(internal)
add_subdirectory(native)
add_subdirectory(platform)

# ------------------------------------------------------------
# Compile, test-hook, and dependency contracts
# ------------------------------------------------------------

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

# ------------------------------------------------------------
# Output, documentation, and installation
# ------------------------------------------------------------

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Desktop
        PAGE_ID desktop_library
        PUBLIC_HEADERS ${DESKTOP_PUBLIC_HEADERS}
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

gamewip_install_package(TARGET Desktop CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DesktopConfig.cmake.in")
