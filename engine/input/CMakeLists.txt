add_library(Input STATIC)

target_sources(Input PRIVATE
    core/input.cpp
)

if(WIN32)
    target_sources(Input PRIVATE
        platform/win32/win32_input.cpp
    )
else()
    message(FATAL_ERROR "Input has no platform backend for this operating system yet.")
endif()

target_include_directories(Input PUBLIC
    "${PROJECT_SOURCE_DIR}/engine"
)

target_compile_features(Input PUBLIC cxx_std_20)