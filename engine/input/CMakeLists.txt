add_library(Input STATIC)

option(INPUT_ENABLE_TEST_HOOKS "Enable Input internal test hooks" OFF)

if(NOT COMMAND gamewip_target_platform_backend)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
endif()

target_sources(Input PRIVATE core/input.cpp)

gamewip_target_platform_backend(TARGET Input ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform")

target_include_directories(Input PUBLIC "${PROJECT_SOURCE_DIR}/engine")

target_compile_features(Input PUBLIC cxx_std_23)
target_compile_definitions(
    Input
    PRIVATE INPUT_INTERNAL_TEST_HOOKS=$<BOOL:${INPUT_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:INPUT_INTERNAL_TEST_HOOKS=$<BOOL:${INPUT_ENABLE_TEST_HOOKS}>>
)
target_link_libraries(Input PRIVATE $<BUILD_INTERFACE:GameWIPBase>)
