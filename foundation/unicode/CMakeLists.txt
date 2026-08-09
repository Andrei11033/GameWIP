include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(Unicode STATIC)
add_library(GameWIP::Unicode ALIAS Unicode)

target_sources(Unicode
    PRIVATE
        core/conversion.cpp
        core/grapheme.cpp
        core/unicode.cpp
        core/utf16.cpp
        core/utf8.cpp
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/unicode.h"
)

target_include_directories(Unicode PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(Unicode PUBLIC cxx_std_23)

set_target_properties(Unicode PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Unicode
        PAGE_ID unicode
        PUBLIC_HEADERS
            "${CMAKE_CURRENT_SOURCE_DIR}/unicode.h"
        DOCS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS Unicode
    EXPORT UnicodeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/UnicodeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/UnicodeConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Unicode
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/UnicodeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

install(EXPORT UnicodeTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Unicode
    NAMESPACE GameWIP::
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/UnicodeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/UnicodeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Unicode
)
