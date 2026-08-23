add_library(WindowManager STATIC)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
endif()

target_sources(WindowManager PRIVATE
    core/window_manager.cpp
)

gamewip_target_platform_backend(TARGET WindowManager ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(WindowManager PUBLIC
    "${PROJECT_SOURCE_DIR}/engine"
)

target_compile_features(WindowManager PUBLIC cxx_std_23)

target_link_libraries(WindowManager PUBLIC
    Input
    Window
)
