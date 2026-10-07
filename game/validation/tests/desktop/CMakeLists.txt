gamewip_add_test_module(
    NAME desktop
    SOURCES desktop_test.cpp accessibility_test.cpp accessibility_test.h desktop_manual_accessibility_tests.inl module.cpp
    LINK_LIBRARIES Desktop Unicode TestSupport
)

if(WIN32)
    target_link_libraries(
        GameWIPTestModules
        PRIVATE shell32 ole32 oleaut32 uiautomationcore uuid
    )
endif()
