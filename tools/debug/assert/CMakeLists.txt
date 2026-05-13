add_library(Assert STATIC)

target_sources(Assert PRIVATE
    core/assert.cpp
)

if(WIN32)
    target_sources(Assert PRIVATE
        platform/win32/win32_assert.cpp
    )
else()
    message(FATAL_ERROR "Assert has no platform backend for this operating system yet.")
endif()

target_include_directories(Assert PUBLIC
    "${PROJECT_SOURCE_DIR}/tools"
)

target_compile_features(Assert PUBLIC cxx_std_20)

target_link_libraries(Assert PUBLIC
    Logger
)
