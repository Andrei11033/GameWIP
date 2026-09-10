cmake_minimum_required(VERSION 4.4.2)

project(GameWIP VERSION 0.1.0 LANGUAGES CXX)

# ------------------------------------------------------------
# Language and platform prerequisites
# ------------------------------------------------------------

# RC is enabled once at the highest common directory for Windows application
# targets that explicitly attach a process manifest.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    enable_language(RC)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ------------------------------------------------------------
# Shared project infrastructure
# ------------------------------------------------------------

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GameWIPVersion)
include(GameWIPOptions)
include(GameWIPPlatform)
include(GameWIPPackage)
include(GameWIPRuntimeDependencies)
include(GameWIPValidationModules)
include(GameWIPCoverage)
include(GameWIPSanitizers)
include(GameWIPWarnings)
include(GameWIPDocumentation)
include(GameWIPStaticAnalysis)
include(GameWIPApplication)

include(CMakePackageConfigHelpers)
set(GAMEWIP_APPLICATION_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/GameWIPApplication")
set(GAMEWIP_APPLICATION_PACKAGE_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/GameWIPApplicationConfig.cmake")
set(GAMEWIP_APPLICATION_PACKAGE_VERSION "${CMAKE_CURRENT_BINARY_DIR}/GameWIPApplicationConfigVersion.cmake")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIPApplicationConfig.cmake.in"
    "${GAMEWIP_APPLICATION_PACKAGE_CONFIG}"
    INSTALL_DESTINATION "${GAMEWIP_APPLICATION_PACKAGE_DIR}"
)
write_basic_package_version_file("${GAMEWIP_APPLICATION_PACKAGE_VERSION}" VERSION "${PROJECT_VERSION}" COMPATIBILITY ExactVersion)
install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIPApplication.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/application_manifest.manifest.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/application_manifest.rc.in"
        "${GAMEWIP_APPLICATION_PACKAGE_CONFIG}"
        "${GAMEWIP_APPLICATION_PACKAGE_VERSION}"
    DESTINATION "${GAMEWIP_APPLICATION_PACKAGE_DIR}"
)

gamewip_configure_version()

gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)

# ------------------------------------------------------------
# Repository-level validation tests
# ------------------------------------------------------------

if(GAMEWIP_BUILD_TESTS)
    enable_testing()
    add_test(
        NAME validation.version.composition
        COMMAND
            "${CMAKE_COMMAND}" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}" -P
            "${PROJECT_SOURCE_DIR}/cmake/TestGameWIPVersion.cmake"
    )
    add_test(
        NAME validation.cmake.requirement_propagation
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}" -P
            "${PROJECT_SOURCE_DIR}/cmake/ValidateCMakeRequirements.cmake"
    )
    set_tests_properties(validation.version.composition validation.cmake.requirement_propagation PROPERTIES TIMEOUT 120)
endif()

# ------------------------------------------------------------
# Project composition
# ------------------------------------------------------------

add_subdirectory(external)

gamewip_enable_project_warnings()

add_subdirectory(foundation)
add_subdirectory(tools)
add_subdirectory(engine)
add_subdirectory(game)

# ------------------------------------------------------------
# Cross-cutting generated targets
# ------------------------------------------------------------

gamewip_create_coverage_target()
gamewip_create_documentation_target()
gamewip_create_static_analysis_targets()
