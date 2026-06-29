gamewip_add_test_module(
    NAME filesystem
    SOURCES
        filesystem_test.cpp
        module.cpp
    LINK_LIBRARIES
        FileSystem
        TestSupport
)
