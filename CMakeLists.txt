cmake_minimum_required(VERSION 4.4)

string(REPLACE "." "\\." gamewip_cmake_version_pattern "${CMAKE_MINIMUM_REQUIRED_VERSION}")
if(NOT CMAKE_VERSION MATCHES "^${gamewip_cmake_version_pattern}(\\.|$)")
    message(FATAL_ERROR
        "GameWIP requires CMake ${CMAKE_MINIMUM_REQUIRED_VERSION}.x; "
        "found ${CMAKE_VERSION}."
    )
endif()
unset(gamewip_cmake_version_pattern)

project(GameWIP VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GameWIPVersion)
include(GameWIPOptions)
include(LibraryPlatform)
include(GameWIPRuntimeDependencies)
include(GameWIPValidationModules)
include(GameWIPCoverage)
include(GameWIPSanitizers)
include(GameWIPDocumentation)
include(GameWIPStaticAnalysis)

gamewip_configure_version()

gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)

if(GAMEWIP_BUILD_TESTS)
    enable_testing()
    add_test(
        NAME validation.version.composition
        COMMAND "${CMAKE_COMMAND}"
            "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/cmake/TestGameWIPVersion.cmake"
    )
    add_test(
        NAME validation.cmake.requirement_propagation
        COMMAND "${CMAKE_COMMAND}"
            "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCMakeRequirements.cmake"
    )
endif()

add_subdirectory(external)
add_subdirectory(foundation)
add_subdirectory(tools)
add_subdirectory(engine)
add_subdirectory(game)

gamewip_create_coverage_target()
gamewip_create_documentation_target()
gamewip_create_static_analysis_targets()
