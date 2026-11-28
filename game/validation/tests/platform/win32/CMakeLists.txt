target_sources(GameWIPTestModules PRIVATE helpers.cpp)

if(TARGET GameWIPTests)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
    target_sources(GameWIPTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()
