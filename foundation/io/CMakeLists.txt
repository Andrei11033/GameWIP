include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(IO STATIC)

target_sources(IO PRIVATE
    core/io.cpp
)

target_include_directories(IO PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(IO PUBLIC cxx_std_23)

set_target_properties(IO PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND library_register_doxygen_library)
    # IO owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified library Doxygen site.
    library_register_doxygen_library(
        NAME IO
        PAGE_ID io
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/io.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS IO
    EXPORT IOTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/io.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/io
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IOConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/IOConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IO
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/IOConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT IOTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IO
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/IOConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/IOConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/IO
)
