gamewip_add_test_module(
    NAME terminal
    SOURCES
        # Module entry points
        terminal_test.cpp
        terminal_test.h
        module.cpp

        # Contracts and output controls
        contracts_test.inl
        controls_test.inl
        formatting_test.inl
        text_output_test.inl
        segment_output_test.inl

        # Input and session behavior
        input_test.inl
        line_input_test.inl
        session_operations_test.inl
        session_input_test.inl
        session_output_test.inl
        session_concurrency_test.inl
        session_lifetime_test.inl
        session_test.inl

        # Manual terminal checks
        manual_input_test.inl
        manual_output_test.inl
        manual_screen_test.inl
        manual_state_test.inl
    LINK_LIBRARIES Terminal TestSupport
)

add_subdirectory(platform)
