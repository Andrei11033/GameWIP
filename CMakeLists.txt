cmake_minimum_required(VERSION 4.4.2)


project(GameWIP VERSION 0.1.0 LANGUAGES CXX)

# RC is enabled once at the highest common directory for Windows targets that
# attach backend-owned manifests and resources.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    enable_language(RC)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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

gamewip_configure_version()

gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)

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

add_subdirectory(external)

gamewip_enable_project_warnings()

add_subdirectory(foundation)
add_subdirectory(tools)
add_subdirectory(engine)
add_subdirectory(game)

gamewip_create_coverage_target()
gamewip_create_documentation_target()
gamewip_create_static_analysis_targets()
