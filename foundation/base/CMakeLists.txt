add_library(GameWIPBase INTERFACE)
add_library(GameWIP::Base ALIAS GameWIPBase)

target_sources(GameWIPBase INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/checked_arithmetic.h>"
    "$<$<AND:$<BUILD_INTERFACE:1>,$<PLATFORM_ID:Windows>>:${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/dynamic_library.h>"
)
target_include_directories(GameWIPBase INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>")
target_compile_features(GameWIPBase INTERFACE cxx_std_23)

