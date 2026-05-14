add_library(WindowManager STATIC)

target_sources(WindowManager PRIVATE
    core/window_manager.cpp
)

if(WIN32)
    target_sources(WindowManager PRIVATE
        platform/win32/win32_window_manager.cpp
    )
else()
    message(FATAL_ERROR "WindowManager has no platform backend for this operating system yet.")
endif()

target_include_directories(WindowManager PUBLIC
    "${PROJECT_SOURCE_DIR}/engine"
)

target_compile_features(WindowManager PUBLIC cxx_std_20)

target_link_libraries(WindowManager PUBLIC
    Input
    Window
)