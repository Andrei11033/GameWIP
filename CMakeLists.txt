cmake_minimum_required(VERSION 4.4.4)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIPTarget.cmake")
gamewip_prepare_target()

project(GameWIP VERSION 0.1.0 LANGUAGES CXX)
gamewip_update_target_native_architecture()

if(PROJECT_IS_TOP_LEVEL)
    set(gamewip_default_install_prefix "${CMAKE_BINARY_DIR}/install")

    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${gamewip_default_install_prefix}" CACHE PATH "GameWIP local install prefix" FORCE)
        set(GAMEWIP_INSTALL_PREFIX_AUTO_DEFAULT ON CACHE INTERNAL "Whether the install prefix follows the build tree" FORCE)
    elseif(GAMEWIP_INSTALL_PREFIX_AUTO_DEFAULT)
        get_filename_component(gamewip_current_install_prefix "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
        get_filename_component(gamewip_previous_install_default "${GAMEWIP_INSTALL_PREFIX_DEFAULT}" ABSOLUTE)

        if(WIN32)
            string(TOLOWER "${gamewip_current_install_prefix}" gamewip_current_install_prefix)
            string(TOLOWER "${gamewip_previous_install_default}" gamewip_previous_install_default)
        endif()

        if(gamewip_current_install_prefix STREQUAL gamewip_previous_install_default)
            set(CMAKE_INSTALL_PREFIX "${gamewip_default_install_prefix}" CACHE PATH "GameWIP local install prefix" FORCE)
        else()
            set(GAMEWIP_INSTALL_PREFIX_AUTO_DEFAULT OFF CACHE INTERNAL "Whether the install prefix follows the build tree" FORCE)
        endif()
    elseif(NOT DEFINED GAMEWIP_INSTALL_PREFIX_AUTO_DEFAULT)
        # Move CMake's former system-wide default while preserving custom prefixes.
        if(WIN32)
            set(gamewip_legacy_install_defaults "C:/Program Files/${PROJECT_NAME}" "C:/Program Files (x86)/${PROJECT_NAME}")

            if(DEFINED ENV{ProgramFiles} AND NOT "$ENV{ProgramFiles}" STREQUAL "")
                get_filename_component(gamewip_program_files_parent "$ENV{ProgramFiles}" DIRECTORY)
                list(
                    APPEND gamewip_legacy_install_defaults
                    "$ENV{ProgramFiles}/${PROJECT_NAME}"
                    "${gamewip_program_files_parent}/Program Files (x86)/${PROJECT_NAME}"
                )
            endif()
            string(TOLOWER "${CMAKE_INSTALL_PREFIX}" gamewip_current_install_prefix)
        else()
            set(gamewip_legacy_install_defaults "/usr/local")
            set(gamewip_current_install_prefix "${CMAKE_INSTALL_PREFIX}")
        endif()

        get_filename_component(gamewip_current_install_prefix "${gamewip_current_install_prefix}" ABSOLUTE)

        if(WIN32)
            string(TOLOWER "${gamewip_current_install_prefix}" gamewip_current_install_prefix)
        endif()

        set(gamewip_legacy_install_default_matches FALSE)

        foreach(gamewip_legacy_install_default IN LISTS gamewip_legacy_install_defaults)
            get_filename_component(gamewip_legacy_install_default "${gamewip_legacy_install_default}" ABSOLUTE)

            if(WIN32)
                string(TOLOWER "${gamewip_legacy_install_default}" gamewip_legacy_install_default)
            endif()

            if(gamewip_current_install_prefix STREQUAL gamewip_legacy_install_default)
                set(gamewip_legacy_install_default_matches TRUE)
                break()
            endif()
        endforeach()

        if(gamewip_legacy_install_default_matches)
            set(CMAKE_INSTALL_PREFIX "${gamewip_default_install_prefix}" CACHE PATH "GameWIP local install prefix" FORCE)
            set(GAMEWIP_INSTALL_PREFIX_AUTO_DEFAULT ON CACHE INTERNAL "Whether the install prefix follows the build tree" FORCE)
        else()
            set(GAMEWIP_INSTALL_PREFIX_AUTO_DEFAULT OFF CACHE INTERNAL "Whether the install prefix follows the build tree" FORCE)
        endif()
    endif()

    set(GAMEWIP_INSTALL_PREFIX_DEFAULT "${gamewip_default_install_prefix}" CACHE INTERNAL "Current default GameWIP install prefix" FORCE)
endif()

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
include(GameWIPDependencies)
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
        "${GAMEWIP_APPLICATION_PACKAGE_CONFIG}"
        "${GAMEWIP_APPLICATION_PACKAGE_VERSION}"
    DESTINATION "${GAMEWIP_APPLICATION_PACKAGE_DIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform/win32/application_manifest.manifest.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform/win32/application_manifest.rc.in"
    DESTINATION "${GAMEWIP_APPLICATION_PACKAGE_DIR}/platform/win32"
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

    if(WIN32 AND (NOT CMAKE_CROSSCOMPILING OR GAMEWIP_TARGET_NATIVE_ARCHITECTURE))
        set(_gamewip_application_validation_name validation.cmake.application_manifest)
        set(_gamewip_application_runtime_probe ON)
    elseif(WIN32)
        set(_gamewip_application_validation_name validation.cmake.application_manifest)
        set(_gamewip_application_runtime_probe OFF)
    else()
        set(_gamewip_application_validation_name validation.cmake.application_helper)
        set(_gamewip_application_runtime_probe OFF)
    endif()
    add_test(
        NAME ${_gamewip_application_validation_name}
        COMMAND
            "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DWORK_DIR=${PROJECT_BINARY_DIR}/application-manifest-tests"
            "-DMODULE_DIR=${PROJECT_SOURCE_DIR}/cmake" "-DGENERATOR=${CMAKE_GENERATOR}" "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}" "-DBUILD_TYPE=$<CONFIG>" "-DGAMEWIP_CMAKE_MINIMUM_VERSION=${CMAKE_MINIMUM_REQUIRED_VERSION}"
            "-DRUN_RUNTIME_PROBE=${_gamewip_application_runtime_probe}" -P "${PROJECT_SOURCE_DIR}/cmake/TestGameWIPApplication.cmake"
    )
    set_tests_properties(${_gamewip_application_validation_name} PROPERTIES LABELS "validation;cmake" TIMEOUT 120)
    unset(_gamewip_application_validation_name)
    unset(_gamewip_application_runtime_probe)
endif()

# ------------------------------------------------------------
# Project composition
# ------------------------------------------------------------

add_subdirectory(external)

gamewip_enable_coverage()
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
