include(GNUInstallDirs)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

option(IO_ENABLE_TEST_HOOKS "Enable IO internal test hooks" OFF)

add_library(IO STATIC)
add_library(GameWIP::IO ALIAS IO)

target_sources(
    IO
    PRIVATE core/io.cpp core/transfer.cpp core/io_test_hooks.cpp
    PUBLIC FILE_SET public_headers TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES "${CMAKE_CURRENT_SOURCE_DIR}/io.h"
)

target_include_directories(IO PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(IO PUBLIC cxx_std_23)
target_compile_definitions(
    IO
    PRIVATE IO_INTERNAL_TEST_HOOKS=$<BOOL:${IO_ENABLE_TEST_HOOKS}>
    INTERFACE $<BUILD_INTERFACE:IO_INTERNAL_TEST_HOOKS=$<BOOL:${IO_ENABLE_TEST_HOOKS}>>
)

target_link_libraries(IO PRIVATE Unicode)

set_target_properties(IO PROPERTIES POSITION_INDEPENDENT_CODE ON ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    # IO owns its public API docs and Markdown manual pages. The root docs
    # target merges this into the unified library Doxygen site.
    gamewip_register_doxygen_library(NAME IO PAGE_ID io PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/io.h" DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
endif()

install(TARGETS IO EXPORT IOTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

gamewip_install_package(TARGET IO CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IOConfig.cmake.in")
