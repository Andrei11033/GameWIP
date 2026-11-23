target_sources(
    Desktop
    PRIVATE
        accessibility.cpp
        accessibility_snapshot.cpp
        accessibility_validation.cpp
        child_surface.cpp
        child_surface_events.cpp
        clipboard.cpp
        cursor.cpp
        dialogs.cpp
        display.cpp
        display_info.cpp
        drag_drop.cpp
        events.cpp
        notification_center.cpp
        progress_dialog.cpp
        renderer_bridge.cpp
        shell.cpp
        taskbar.cpp
        tray.cpp
        window.cpp
)

if(DESKTOP_ENABLE_TEST_HOOKS)
    target_sources(Desktop PRIVATE test_hooks.cpp)
endif()
