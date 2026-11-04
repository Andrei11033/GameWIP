# Keep the opt-in Win32 API linkable while reporting that its backend is unavailable.
target_sources(Input PRIVATE "${CMAKE_CURRENT_LIST_DIR}/attachment_stub.cpp")
