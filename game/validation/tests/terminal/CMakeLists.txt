gamewip_add_test_module(
    NAME terminal
    SOURCES
        terminal_test.cpp
        module.cpp
    LINK_LIBRARIES
        Terminal
        TestSupport
)
