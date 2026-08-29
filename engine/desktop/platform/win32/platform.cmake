# Own Desktop's Win32 sources, native API, libraries, resources, and documentation inputs.
set(DESKTOP_INTERNAL_APPLICATION_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../../cmake/desktop_application.manifest")
set(DESKTOP_INTERNAL_APPLICATION_RC "${CMAKE_CURRENT_BINARY_DIR}/desktop_application.rc")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../../cmake/desktop_application.rc.in" "${DESKTOP_INTERNAL_APPLICATION_RC}" @ONLY)

target_sources(
    Desktop
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32_clipboard.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_child_surface.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_controls.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_cursor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_mode.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_monitor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_operations.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_unicode.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window_dispatch.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window_lifecycle.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window_proc.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_compat.h"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_window_backend.h"
)

target_sources(
    Desktop
    INTERFACE "$<BUILD_INTERFACE:${DESKTOP_INTERNAL_APPLICATION_RC}>"
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../.." FILES "${CMAKE_CURRENT_LIST_DIR}/../../native/win32.h"
)

target_link_libraries(
    Desktop
    PRIVATE dwmapi dxgi dxguid gdi32 shell32 shcore user32
)

if(MINGW)
    target_link_options(Desktop PRIVATE "-Wl,--exclude-all-symbols")
endif()

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_inputs)
    gamewip_register_doxygen_inputs("${CMAKE_CURRENT_LIST_DIR}/../../native/win32.h")
endif()

install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/../../cmake/desktop_application.manifest" "${CMAKE_CURRENT_LIST_DIR}/../../cmake/desktop_application.rc.in"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/Desktop
)
