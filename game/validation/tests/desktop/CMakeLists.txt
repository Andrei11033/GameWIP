gamewip_add_test_module(
    NAME desktop
    SOURCES
        desktop_test.cpp
        accessibility_test.cpp
        accessibility_test.h
        accessibility_contract_tests.inl
        accessibility_com_fixtures.inl
        accessibility_transport_tests.inl
        accessibility_provider_tests.inl
        accessibility_text_tests.inl
        accessibility_retained_tests.inl
        accessibility_control_tests.inl
        accessibility_geometry_tests.inl
        desktop_manual_accessibility_tests.inl
        module.cpp
    LINK_LIBRARIES Desktop Unicode TestSupport
)

if(WIN32)
    target_link_libraries(
        GameWIPTestModules
        PRIVATE shell32 ole32 oleaut32 uiautomationcore uuid
    )
endif()
