target_sources(
    Desktop
    PRIVATE
        win32_accessibility.cpp
        win32_accessibility_lifecycle.cpp
        win32_accessibility_patterns.cpp
        win32_accessibility_text.cpp
        win32_child_surface.cpp
        win32_clipboard.cpp
        win32_controls.cpp
        win32_cursor.cpp
        win32_data_transfer.cpp
        win32_dialogs.cpp
        win32_drag_drop.cpp
        win32_mode.cpp
        win32_monitor.cpp
        win32_notification_center.cpp
        win32_operations.cpp
        win32_progress_dialog.cpp
        win32_shell_jump_lists.cpp
        win32_shell_registration.cpp
        win32_taskbar.cpp
        win32_tray.cpp
        win32_window.cpp
        win32_window_dispatch.cpp
        win32_window_lifecycle.cpp
        win32_window_proc.cpp
)

target_link_libraries(
    Desktop
    PRIVATE advapi32 comctl32 dwmapi dxgi dxguid gdi32 ole32 oleaut32 shell32 shcore user32 uuid uiautomationcore
)

if(MINGW)
    target_link_options(Desktop PRIVATE "-Wl,--exclude-all-symbols")
endif()

add_subdirectory(internal)
