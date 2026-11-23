target_sources(
    GameWIPTestModules
    PRIVATE
        # Accessibility validation
        accessibility_test.cpp
        accessibility_contract_tests.inl
        accessibility_com_fixtures.inl
        accessibility_transport_tests.inl
        accessibility_provider_tests.inl
        accessibility_text_ranges_tests.inl
        accessibility_text_semantics_tests.inl
        accessibility_text_attributes_tests.inl
        accessibility_retained_tests.inl
        accessibility_control_tests.inl
        accessibility_geometry_tests.inl
        # Module entry points and child-process routing
        desktop_test.cpp
        desktop_process_tests.inl
        child_arguments.cpp
        # Dialog validation
        desktop_file_dialog_tests.inl
        desktop_apartment_tests.inl
        desktop_message_dialog_tests.inl
        desktop_prompt_dialog_tests.inl
        desktop_progress_dialog_support.inl
        desktop_progress_dialog_tests.inl
        desktop_progress_dialog_ownership_tests.inl
        # Window contracts
        desktop_values_tests.inl
        desktop_lifecycle_tests.inl
        desktop_native_window_tests.inl
        desktop_event_tests.inl
        desktop_child_surface_tests.inl
        # Renderer, display, and cursor behavior
        desktop_presentation_tests.inl
        desktop_pointer_hit_mask_tests.inl
        desktop_renderer_occlusion_tests.inl
        desktop_display_tests.inl
        desktop_cursor_support.inl
        desktop_cursor_selection_tests.inl
        desktop_cursor_native_tests.inl
        # Clipboard, drag-and-drop, and shell behavior
        desktop_clipboard_support.inl
        desktop_clipboard_contract_tests.inl
        desktop_clipboard_roundtrip_tests.inl
        desktop_clipboard_failure_tests.inl
        desktop_drag_drop_tests.inl
        desktop_shell_event_tests.inl
        desktop_shell_lifecycle_tests.inl
        desktop_jump_list_tests.inl
        desktop_shell_registration_tests.inl
        desktop_shell_application_registration_tests.inl
        desktop_shell_launch_tests.inl
        # Manual scenario support
        desktop_manual_fullscreen_support.inl
        desktop_manual_status.inl
        desktop_manual_transfer_support.inl
        desktop_manual_surface.inl
        desktop_manual_interaction.inl
        desktop_manual_window_setup.inl
        desktop_manual_drag_drop_support.inl
        desktop_manual_shell_launch_support.inl
        # Manual scenario validation
        desktop_manual_lifecycle_tests.inl
        desktop_manual_chrome_tests.inl
        desktop_manual_dpi_cursor_tests.inl
        desktop_manual_accessibility_tests.inl
        desktop_manual_child_surface_tests.inl
        desktop_manual_clipboard_tests.inl
        desktop_manual_focused_dialog_tests.inl
        desktop_manual_dialog_tests.inl
        desktop_manual_file_drop_tests.inl
        desktop_manual_shell_tests.inl
        desktop_manual_window_policy_tests.inl
        desktop_manual_files_shell_tests.inl
        desktop_manual_drag_drop_tests.inl
        desktop_manual_fullscreen_tests.inl
        desktop_manual_color_tests.inl
        desktop_manual_modern_capability_tests.inl
)

target_link_libraries(
    GameWIPTestModules
    PRIVATE shell32 ole32 oleaut32 uiautomationcore uuid
)
