cmake_minimum_required(VERSION 3.20)

project(GameWIP VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project-level build options. Runtime test selection is controlled by TestRunOptions in game/main.cpp.
option(GAMEWIP_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(GAMEWIP_ENABLE_TOOLS "Enable editor/tool window support in the game executable" OFF)
option(GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP "Open tool/debug windows at startup when tool support is enabled" OFF)
option(GAMEWIP_ENABLE_ASSERTS "Enable GameWIP assertions and recoverable checks" ON)
option(GAMEWIP_ENABLE_COVERAGE "Enable coverage instrumentation for GameWIP tests" OFF)
option(GAMEWIP_ENABLE_LOGGER_TEST_HOOKS "Enable Logger internal test hooks" OFF)
option(GAMEWIP_ENABLE_ASSERT_TEST_HOOKS "Enable Assert internal test hooks" OFF)
option(GAMEWIP_BUILD_DOCS "Build GameWIP Doxygen documentation" OFF)
option(GAMEWIP_INSTALL_DOCS "Install generated Doxygen HTML documentation" OFF)

if(GAMEWIP_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    else()
        message(WARNING "GAMEWIP_ENABLE_COVERAGE is currently only configured for GCC/Clang-style toolchains.")
    endif()
endif()

if(GAMEWIP_ENABLE_COVERAGE)
    set(GAMEWIP_COVERAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
    find_program(GAMEWIP_GCOVR_EXECUTABLE gcovr)

    if(GAMEWIP_GCOVR_EXECUTABLE)
        message(STATUS "Coverage reports enabled with gcovr: ${GAMEWIP_GCOVR_EXECUTABLE}")

        set(GAMEWIP_COVERAGE_COMMON_ARGS
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
            --filter "${CMAKE_CURRENT_SOURCE_DIR}/tools/logger"
            --filter "${CMAKE_CURRENT_SOURCE_DIR}/tools/debug/assert"
            --filter "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_support"
            --filter "${CMAKE_CURRENT_SOURCE_DIR}/game/test"
            --exclude "${CMAKE_CURRENT_SOURCE_DIR}/external"
            --gcov-ignore-parse-errors negative_hits.warn_once_per_file
        )

        set(GAMEWIP_COVERAGE_STALE_ARTIFACT_CLEAN_COMMANDS)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/test/testing_test.cpp")
            list(APPEND GAMEWIP_COVERAGE_STALE_ARTIFACT_CLEAN_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E rm -f
                    "${CMAKE_BINARY_DIR}/CMakeFiles/GameWIP.dir/game/test/testing_test.cpp.gcda"
                    "${CMAKE_BINARY_DIR}/CMakeFiles/GameWIP.dir/game/test/testing_test.cpp.gcno"
            )
        endif()
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/logger/core/logger.cpp")
            list(APPEND GAMEWIP_COVERAGE_STALE_ARTIFACT_CLEAN_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E rm -f
                    "${CMAKE_BINARY_DIR}/tools/logger/CMakeFiles/Logger.dir/core/logger.cpp.gcda"
                    "${CMAKE_BINARY_DIR}/tools/logger/CMakeFiles/Logger.dir/core/logger.cpp.gcno"
            )
        endif()
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/testing")
            list(APPEND GAMEWIP_COVERAGE_STALE_ARTIFACT_CLEAN_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/tools/testing"
            )
        endif()

        add_custom_target(coverage
            ${GAMEWIP_COVERAGE_STALE_ARTIFACT_CLEAN_COMMANDS}
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${GAMEWIP_COVERAGE_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${GAMEWIP_COVERAGE_OUTPUT_DIR}"
            COMMAND "${GAMEWIP_GCOVR_EXECUTABLE}"
                ${GAMEWIP_COVERAGE_COMMON_ARGS}
                --html-details
                --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/index.html"
            COMMAND "${GAMEWIP_GCOVR_EXECUTABLE}"
                ${GAMEWIP_COVERAGE_COMMON_ARGS}
                --xml-pretty
                --output "${GAMEWIP_COVERAGE_OUTPUT_DIR}/coverage.xml"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Generating GameWIP coverage report with gcovr"
            VERBATIM
        )
    else()
        message(WARNING "GAMEWIP_ENABLE_COVERAGE is ON, but gcovr was not found. Install it with: python -m pip install gcovr. The 'coverage' target will fail with this message until gcovr is available.")
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E echo "GAMEWIP_ENABLE_COVERAGE is ON, but gcovr was not found."
            COMMAND ${CMAKE_COMMAND} -E echo "Install it with: python -m pip install gcovr"
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "Coverage report unavailable because gcovr is missing"
            VERBATIM
        )
    endif()
endif()

if(GAMEWIP_INSTALL_DOCS AND NOT GAMEWIP_BUILD_DOCS)
    message(FATAL_ERROR "GAMEWIP_INSTALL_DOCS requires GAMEWIP_BUILD_DOCS=ON so the generated HTML exists before install.")
endif()

if(GAMEWIP_BUILD_DOCS)
    find_package(Doxygen QUIET)
    if(NOT DOXYGEN_FOUND)
        message(FATAL_ERROR "GAMEWIP_BUILD_DOCS is ON, but Doxygen was not found. Install Doxygen and reconfigure, or set GAMEWIP_BUILD_DOCS=OFF.")
    endif()

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIPDoxygen.cmake")

    set(GAMEWIP_DOXYGEN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen")
    set(GAMEWIP_DOXYGEN_MAINPAGE "${GAMEWIP_DOXYGEN_ROOT}/index.md")

    # Project-wide Doxygen pages live under docs/doxygen. Register explicit
    # Markdown files so Doxygen does not crawl generated HTML or private notes.
    gamewip_register_doxygen_inputs(
        "${GAMEWIP_DOXYGEN_ROOT}/index.md"
        "${GAMEWIP_DOXYGEN_ROOT}/build.md"
        "${GAMEWIP_DOXYGEN_ROOT}/coverage.md"
        "${GAMEWIP_DOXYGEN_ROOT}/documentation.md"
        "${GAMEWIP_DOXYGEN_ROOT}/testing.md"
        "${GAMEWIP_DOXYGEN_ROOT}/mainpage.md"
    )
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GameWIPPlatform.cmake")
gamewip_resolve_platform_id(GAMEWIP_PLATFORM_ID)

set(TRACY_ENABLE ${GAMEWIP_ENABLE_TRACY} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

add_subdirectory(external/tracy)

set(LOGGER_TEST_HOOKS ${GAMEWIP_ENABLE_LOGGER_TEST_HOOKS} CACHE BOOL "Enable Logger internal test hooks" FORCE)
set(ASSERT_TEST_HOOKS ${GAMEWIP_ENABLE_ASSERT_TEST_HOOKS} CACHE BOOL "Enable Assert internal test hooks" FORCE)

add_subdirectory(tools/logger)

if(GAMEWIP_ENABLE_ASSERTS)
    set(ASSERT_ENABLED ON CACHE STRING "Force assertions on, off, or let NDEBUG choose" FORCE)
    set(ASSERT_CHECKS_ENABLED ON CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose" FORCE)
else()
    set(ASSERT_ENABLED OFF CACHE STRING "Force assertions on, off, or let NDEBUG choose" FORCE)
    set(ASSERT_CHECKS_ENABLED OFF CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose" FORCE)
endif()

add_subdirectory(tools/debug/assert)
add_subdirectory(tools/test_support)
add_subdirectory(engine/input)
add_subdirectory(engine/action)
add_subdirectory(engine/window)
add_subdirectory(engine/window_manager)

if(GAMEWIP_BUILD_DOCS)
    gamewip_create_doxygen_target(
        MAINPAGE "${GAMEWIP_DOXYGEN_MAINPAGE}"
    )
endif()

add_executable(GameWIP
    game/main.cpp
    game/test/test_support_test.cpp
    game/test/assert_test.cpp
    game/test/logger_test.cpp
)

if(GAMEWIP_ENABLE_TOOLS)
    target_compile_definitions(GameWIP PRIVATE GAMEWIP_ENABLE_TOOLS=1)
    if(GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP)
        target_compile_definitions(GameWIP PRIVATE GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP=1)
    endif()
endif()

target_link_libraries(GameWIP PRIVATE
    GameWIP::TestSupport
    GameWIP::Logger
    GameWIP::Assert
    Input
    Window
    WindowManager
    Action
    Tracy::TracyClient
)

target_include_directories(GameWIP PRIVATE
    game
    tools
)

# Use the compiler folder as the first place to search for runtime DLLs.
get_filename_component(MSYS2_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

# Copy runtime DLLs next to GameWIP.exe so it can run without depending on PATH.
add_custom_command(TARGET GameWIP POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DGAMEWIP_EXECUTABLE=$<TARGET_FILE:GameWIP>"
        "-DGAMEWIP_OUTPUT_DIR=$<TARGET_FILE_DIR:GameWIP>"
        "-DGAMEWIP_RUNTIME_SEARCH_DIR=${MSYS2_RUNTIME_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_runtime_dependencies.cmake"
    VERBATIM
)

enable_testing()
add_test(NAME GameWIPTests
    COMMAND $<TARGET_FILE:GameWIP> --no-manual-ui
)
