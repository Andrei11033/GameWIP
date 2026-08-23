# Own Terminal's Win32 implementation sources and private backend declarations.
target_sources(Terminal PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/win32_terminal.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/win32_terminal_events.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/win32_terminal_events.h"
)
