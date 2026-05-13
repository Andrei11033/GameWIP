add_library(Window STATIC)

target_sources(Window PRIVATE
    core/window.cpp
)

if(WIN32)
    target_sources(Window PRIVATE
        platform/win32/win32_window.cpp
    )
else()
    message(FATAL_ERROR "Window has no platform backend for this operating system yet.")
endif()

target_include_directories(Window PUBLIC
    "${PROJECT_SOURCE_DIR}/engine"
)

target_compile_features(Window PUBLIC cxx_std_20)

target_link_libraries(Window PUBLIC
    Input
)

target_link_libraries(Window PRIVATE
    shell32
    Tracy::TracyClient
)
