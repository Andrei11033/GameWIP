target_sources(Logger PRIVATE win32_logger.cpp)
target_link_libraries(Logger PRIVATE psapi)
