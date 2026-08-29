gamewip_add_test_module(NAME desktop SOURCES desktop_test.cpp module.cpp LINK_LIBRARIES Desktop TestSupport)

if(WIN32)
    target_link_libraries(GameWIPTestModules PRIVATE shell32)
endif()
