gamewip_add_test_module(
    NAME status
    SOURCES
        # Module entry points
        module.cpp
        status_test.cpp
        status_test.h
        # Status and Result contracts
        status_contracts_test.inl
    LINK_LIBRARIES Status TestSupport
)
