gamewip_add_test_module(
    NAME logger
    SOURCES
        # Module entry points
        logger_test.cpp
        logger_test.h
        module.cpp
        # Shared fixtures and test suites
        logger_test_support.inl
        configuration_test.inl
        output_test.inl
        record_layout_test.inl
        filters_test.inl
        reports_test.inl
        health_test.inl
        process_test.inl
    LINK_LIBRARIES FileSystem Logger Terminal TestSupport Unicode
)
