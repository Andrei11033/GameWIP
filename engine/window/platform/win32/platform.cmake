# Win32 libraries are backend-local implementation dependencies.
target_sources(Window PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/win32_controls.cpp"
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

target_link_libraries(Window PRIVATE
    dwmapi
    dxgi
    dxguid
    gdi32
    shell32
    shcore
    user32
)
