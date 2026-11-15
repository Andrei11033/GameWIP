# Own Input's Win32 attachment implementation and native dependencies.
target_sources(
    Input
    PRIVATE
        # Attachment and window event handling
        "${CMAKE_CURRENT_LIST_DIR}/attachment.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_attachment_messages.cpp"

        # Input providers
        "${CMAKE_CURRENT_LIST_DIR}/win32_directinput.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_gameinput.cpp"

        # Device discovery and event capture
        "${CMAKE_CURRENT_LIST_DIR}/win32_devices.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/win32_events.cpp"

        # Shared internal contracts
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_attachment_state.h"
        "${CMAKE_CURRENT_LIST_DIR}/internal/win32_keyboard_mapping.h"
)

if(NOT COMMAND gamewip_prepare_archive_dependency)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/GameWIPDependencies.cmake")
endif()

gamewip_prepare_archive_dependency(gameinput GAMEWIP_GAMEINPUT_PACKAGE_DIR)
target_include_directories(Input SYSTEM PRIVATE "${GAMEWIP_GAMEINPUT_PACKAGE_DIR}/native/include/v1")

target_link_libraries(Input PRIVATE kernel32 user32 hid imm32 setupapi ole32 dxguid)
