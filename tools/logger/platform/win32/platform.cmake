# Own Logger's Win32 implementation source and process-status library.
target_sources(Logger PRIVATE "${CMAKE_CURRENT_LIST_DIR}/win32_logger.cpp")

target_link_libraries(Logger PRIVATE
    psapi
)
