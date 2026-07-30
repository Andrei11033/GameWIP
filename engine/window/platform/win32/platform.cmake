# Win32 libraries are backend-local implementation dependencies.
target_link_libraries(Window PRIVATE
    dwmapi
    gdi32
    shell32
    shcore
    user32
)
