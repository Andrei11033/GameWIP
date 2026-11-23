target_sources(Terminal PRIVATE backend.h)

gamewip_add_platform_backend(TARGET Terminal ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
