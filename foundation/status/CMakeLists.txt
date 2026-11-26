# Status provides dependency-neutral operation results for GameWIP libraries.
include(GNUInstallDirs)

if(NOT COMMAND gamewip_install_package)
    include("${PROJECT_SOURCE_DIR}/cmake/GameWIPPackage.cmake")
endif()

# ------------------------------------------------------------
# Target and public headers
# ------------------------------------------------------------

add_library(Status INTERFACE)
add_library(GameWIP::Status ALIAS Status)

target_sources(
    Status
    INTERFACE FILE_SET public_headers TYPE HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/status.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/result.h"
)

target_include_directories(Status INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(Status INTERFACE cxx_std_23)

# ------------------------------------------------------------
# Documentation and installation
# ------------------------------------------------------------

if(GAMEWIP_BUILD_DOCS AND COMMAND gamewip_register_doxygen_library)
    gamewip_register_doxygen_library(
        NAME Status
        PAGE_ID status
        PUBLIC_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/status.h" "${CMAKE_CURRENT_SOURCE_DIR}/result.h"
        DOCS "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    )
endif()

install(TARGETS Status EXPORT StatusTargets FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
gamewip_install_package(TARGET Status CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StatusConfig.cmake.in")
