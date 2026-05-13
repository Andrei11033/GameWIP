add_library(Logger SHARED)

target_sources(Logger PRIVATE
    core/logger.cpp
)

if(WIN32)
    target_sources(Logger PRIVATE
        platform/win32/win32_logger.cpp
    )
else()
    message(FATAL_ERROR "Logger has no platform backend for this operating system yet.")
endif()

target_include_directories(Logger PUBLIC
    "${PROJECT_SOURCE_DIR}/tools"
)

target_compile_features(Logger PUBLIC cxx_std_20)
target_compile_definitions(Logger PRIVATE GAMEWIP_LOGGER_BUILD)

set_target_properties(Logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
