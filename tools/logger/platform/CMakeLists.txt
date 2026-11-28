target_sources(Logger PRIVATE backend.h)

gamewip_add_platform_backend(TARGET Logger ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
