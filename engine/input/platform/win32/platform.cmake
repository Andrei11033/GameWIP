# Own Input's Win32 attachment implementation and native dependencies.
target_sources(
    Input
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/attachment.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_devices.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_events.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_attachment_state.h"
)

target_link_libraries(Input PRIVATE kernel32 user32 hid imm32)
