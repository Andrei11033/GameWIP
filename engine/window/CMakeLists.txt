include(GNUInstallDirs)
include(GenerateExportHeader)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

option(WINDOW_ENABLE_TEST_HOOKS "Enable Window internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
    gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)
endif()

# Window intentionally remains shared-only. Its process-local identities, native-class
# registration, Window registry, and per-thread dispatcher registry must have one coherent
# runtime instance when the library is consumed across executable/DLL boundaries.
add_library(Window SHARED)
add_library(GameWIP::Window ALIAS Window)

set(WINDOW_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${WINDOW_GENERATED_INCLUDE_DIR}/window")
generate_export_header(
    Window
    BASE_NAME GAMEWIP_WINDOW
    EXPORT_FILE_NAME "${WINDOW_GENERATED_INCLUDE_DIR}/window/window_export.h"
    EXPORT_MACRO_NAME GAMEWIP_WINDOW_EXPORT
    NO_EXPORT_MACRO_NAME GAMEWIP_WINDOW_NO_EXPORT
    STATIC_DEFINE GAMEWIP_WINDOW_STATIC_DEFINE
)

target_sources(
    Window
    PRIVATE core/display.cpp core/display_info.cpp core/events.cpp core/renderer_bridge.cpp core/window.cpp core/window_test_hooks.cpp
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." "${WINDOW_GENERATED_INCLUDE_DIR}"
            FILES
                "${CMAKE_CURRENT_SOURCE_DIR}/description.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/display.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/display_info.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/events.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/renderer_bridge.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
                "${WINDOW_GENERATED_INCLUDE_DIR}/window/window_export.h"
)

gamewip_target_platform_backend(TARGET Window ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(
    Window
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${WINDOW_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Window PUBLIC cxx_std_23)
target_compile_definitions(
    Window
    PRIVATE WINDOW_INTERNAL_TEST_HOOKS=$<BOOL:${WINDOW_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:WINDOW_INTERNAL_TEST_HOOKS=$<BOOL:${WINDOW_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(Window PUBLIC IO FileSystem PRIVATE Unicode $<BUILD_INTERFACE:GameWIPBase>)

set_target_properties(
    Window
    PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Window
        PAGE_ID window_library
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/description.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/display.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/display_info.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/events.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/renderer_bridge.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/types.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/window.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(
    TARGETS Window
    EXPORT WindowTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

gamewip_install_package(TARGET Window CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WindowConfig.cmake.in" PATH_VARS CMAKE_INSTALL_DATADIR)
