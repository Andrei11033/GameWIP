gamewip_add_test_module(
    NAME test_support
    SOURCES
        # Module entry points
        module.cpp
        test_support_test.cpp
        test_support_test.h
        # Test suites
        child_modes_test.inl
        context_test.inl
        environment_test.inl
        files_test.inl
        process_test.inl
        prompt_test.inl
        report_modes_test.inl
        runner_test.inl
        stress_test.inl
    LINK_LIBRARIES TestSupport
)

add_subdirectory(platform)
