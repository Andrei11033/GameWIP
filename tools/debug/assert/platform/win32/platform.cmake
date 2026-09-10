# Own Assert's Win32 runtime and library dependencies.
if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_sources(Assert PRIVATE "${CMAKE_CURRENT_LIST_DIR}/win32_assert.cpp")
endif()

if(ASSERT_RUNTIME_TARGET_ENABLED)
    target_link_libraries(Assert PRIVATE comctl32)
endif()
