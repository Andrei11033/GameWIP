add_library(Logger SHARED)
add_library(GameWIP::Logger ALIAS Logger)

target_sources(Logger PRIVATE
    core/logger.cpp
)

if(WIN32)
    target_sources(Logger PRIVATE
        platform/win32/win32_logger.cpp
    )
    target_link_libraries(Logger PRIVATE
        psapi
    )
else()
    message(FATAL_ERROR "Logger has no platform backend for this operating system yet.")
endif()

include(GNUInstallDirs)

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/logs" LOGGER_DEFAULT_DIRECTORY_PATH)

target_include_directories(Logger PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/tools>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Logger PUBLIC cxx_std_20)
target_compile_definitions(Logger PRIVATE
    LOGGER_BUILD
    LOGGER_DEFAULT_DIRECTORY="${LOGGER_DEFAULT_DIRECTORY_PATH}"
)

set_target_properties(Logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)


install(TARGETS Logger
    EXPORT GameWIPLoggerTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/logger"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "internal" EXCLUDE
)

install(EXPORT GameWIPLoggerTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIPLogger
)
