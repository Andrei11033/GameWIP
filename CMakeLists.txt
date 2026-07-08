cmake_minimum_required(VERSION 3.23)

project(GameWIP VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GameWIPOptions)
include(LibraryPlatform)
include(GameWIPRuntimeDependencies)
include(GameWIPValidationModules)
include(GameWIPCoverage)
include(GameWIPSanitizers)
include(GameWIPDocumentation)
include(GameWIPStaticAnalysis)

gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)

if(GAMEWIP_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(external)
add_subdirectory(foundation)
add_subdirectory(tools)
add_subdirectory(engine)
add_subdirectory(game)

gamewip_create_coverage_target()
gamewip_create_documentation_target()
gamewip_create_static_analysis_targets()
