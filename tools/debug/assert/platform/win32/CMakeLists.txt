if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_sources(Assert PRIVATE win32_assert.cpp)
    target_link_libraries(Assert PRIVATE comctl32)
endif()
