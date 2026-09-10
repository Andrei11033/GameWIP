# Own Desktop's Win32 sources, native API, libraries, and documentation inputs.
target_sources(
    Desktop
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32_clipboard.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_child_surface.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_controls.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_cursor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_data_transfer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_drag_drop.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_mode.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_monitor.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_operations.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_unicode.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window_dispatch.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window_lifecycle.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_window_proc.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_compat.h"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_data_transfer.h"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_window_backend.h"
)

target_sources(
    Desktop
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../.." FILES "${CMAKE_CURRENT_LIST_DIR}/../../native/win32.h"
)

target_link_libraries(
    Desktop
    PRIVATE dwmapi dxgi dxguid gdi32 ole32 shell32 shcore user32
)

if(MINGW)
    target_link_options(Desktop PRIVATE "-Wl,--exclude-all-symbols")
endif()

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_inputs)
    gamewip_register_doxygen_inputs("${CMAKE_CURRENT_LIST_DIR}/../../native/win32.h")
endif()
