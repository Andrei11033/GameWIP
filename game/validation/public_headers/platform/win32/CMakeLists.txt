gamewip_add_public_header_check(DesktopNativeWin32 "${CMAKE_CURRENT_SOURCE_DIR}/desktop_native_win32_header.cpp" GameWIP::Desktop)
gamewip_add_public_header_check(
    InputNativeDesktopWin32
    "${CMAKE_CURRENT_SOURCE_DIR}/input_native_desktop_win32_header.cpp"
    GameWIP::Input
    GameWIP::Desktop
)
gamewip_add_public_header_check(InputWin32 "${CMAKE_CURRENT_SOURCE_DIR}/input_win32_header.cpp" GameWIP::Input)
