target_sources(
    Input
    PRIVATE
        attachment.cpp
        win32_attachment_messages.cpp
        win32_devices.cpp
        win32_directinput.cpp
        win32_directinput_poll.cpp
        win32_events.cpp
        win32_gameinput.cpp
        win32_hid.cpp
        win32_hid_reports.cpp
        win32_text.cpp
)

add_subdirectory(internal)

if(NOT COMMAND gamewip_prepare_archive_dependency)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPDependencies.cmake")
endif()

gamewip_prepare_archive_dependency(gameinput GAMEWIP_GAMEINPUT_PACKAGE_DIR)
target_include_directories(Input SYSTEM PRIVATE "${GAMEWIP_GAMEINPUT_PACKAGE_DIR}/native/include/v1")
target_link_libraries(
    Input
    PRIVATE kernel32 user32 hid imm32 setupapi ole32 dxguid
)
