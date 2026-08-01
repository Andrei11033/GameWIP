gamewip_add_test_module(
    NAME window
    SOURCES
        window_test.cpp
        module.cpp
    LINK_LIBRARIES
        Window
        TestSupport
)

if(WIN32)
    target_link_libraries(GameWIPTestModules PRIVATE shell32)
endif()
