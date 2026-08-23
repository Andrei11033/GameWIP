include(GNUInstallDirs)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

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
target_link_libraries(Unicode PRIVATE $<BUILD_INTERFACE:GameWIPBase>)

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

gamewip_install_package(TARGET Unicode CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/UnicodeConfig.cmake.in")
