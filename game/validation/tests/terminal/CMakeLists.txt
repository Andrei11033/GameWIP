gamewip_add_test_module(
    NAME terminal
    SOURCES
        # Module entry points
        terminal_test.cpp
        terminal_test.h
        module.cpp

        # Shared test suites
        contracts_test.inl
        controls_test.inl
        input_test.inl
        line_input_test.inl
        manual_output_test.inl
        manual_screen_test.inl
        manual_input_test.inl
        manual_state_test.inl
        formatting_test.inl
        text_output_test.inl
        segment_output_test.inl
        session_test.inl
    LINK_LIBRARIES Terminal TestSupport
)

add_subdirectory(platform)
