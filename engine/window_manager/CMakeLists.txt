add_library(WindowManager STATIC)

if(NOT COMMAND gamewip_add_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
endif()

add_subdirectory(core)
add_subdirectory(platform)

target_include_directories(WindowManager PUBLIC "${PROJECT_SOURCE_DIR}/engine")

target_compile_features(WindowManager PUBLIC cxx_std_23)

target_link_libraries(WindowManager PUBLIC Input Window)
