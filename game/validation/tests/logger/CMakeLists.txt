gamewip_add_test_module(
    NAME logger
    SOURCES
        logger_test.cpp
        module.cpp
    LINK_LIBRARIES
        Logger
        Terminal
        TestSupport
        Unicode
)
