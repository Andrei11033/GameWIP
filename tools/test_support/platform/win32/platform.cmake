# Own TestSupport's Win32 child-process and environment implementation sources.
target_sources(TestSupport PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/win32_child_process.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/win32_environment.cpp"
)
