target_sources(
    GameWIPTestModules
    PRIVATE
        # Accessibility validation
        accessibility_test.cpp
        accessibility_contract_tests.inl
        accessibility_com_fixtures.inl
        accessibility_transport_tests.inl
        accessibility_provider_tests.inl
        accessibility_text_tests.inl
        accessibility_retained_tests.inl
        accessibility_control_tests.inl
        accessibility_geometry_tests.inl

        # Window and shell behavior
        child_arguments.cpp
        desktop_test.cpp
        desktop_child_surface_tests.inl
        desktop_clipboard_tests.inl
        desktop_cursor_tests.inl
        desktop_dialog_tests.inl
        desktop_display_tests.inl
        desktop_drag_drop_tests.inl
        desktop_event_tests.inl
        desktop_lifecycle_tests.inl
        desktop_manual_accessibility_tests.inl
        desktop_manual_clipboard_tests.inl
        desktop_manual_dialog_tests.inl
        desktop_manual_display_tests.inl
        desktop_manual_support.inl
        desktop_manual_transfer_tests.inl
        desktop_manual_window_tests.inl
        desktop_progress_dialog_tests.inl
        desktop_renderer_tests.inl
        desktop_shell_tests.inl
)

target_link_libraries(GameWIPTestModules PRIVATE shell32 ole32 oleaut32 uiautomationcore uuid)
