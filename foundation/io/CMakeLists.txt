include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(IO STATIC)
add_library(GameWIP::IO ALIAS IO)

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

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # IO owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified GameWIP Doxygen site.
    gamewip_register_doxygen_library(
        NAME IO
        PAGE_ID foundation_io
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/io.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS IO
    EXPORT GameWIPIOTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/io.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/io
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIP_IOConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_IOConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_IO
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_IOConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT GameWIPIOTargets
    NAMESPACE GameWIP::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_IO
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_IOConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/GameWIP_IOConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GameWIP_IO
)
