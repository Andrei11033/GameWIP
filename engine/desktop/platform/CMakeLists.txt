target_sources(
    Desktop
    PRIVATE child_surface.h clipboard.h cursor.h dialogs.h drag_drop.h shell.h window.h
)

gamewip_add_platform_backend(TARGET Desktop ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
