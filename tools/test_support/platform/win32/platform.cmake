# This backend owns the Win32 implementation sources; the common target remains
# responsible for the public headers, dependency boundary, and package export.
target_sources(TestSupport PRIVATE "${CMAKE_CURRENT_LIST_DIR}/win32_child_process.cpp" "${CMAKE_CURRENT_LIST_DIR}/win32_environment.cpp")
