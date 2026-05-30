cmake_minimum_required(VERSION 3.20)

project(GameWIP VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Option to enable Tracy profiler instrumentation.
option(GAMEWIP_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(GAMEWIP_ENABLE_TOOLS "Enable editor/tool window support in the game executable" OFF)
option(GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP "Open tool/debug windows at startup when tool support is enabled" OFF)

# Option for Assert
option(GAMEWIP_ENABLE_ASSERTS "Enable GameWIP assertions and recoverable checks" ON)

set(TRACY_ENABLE ${GAMEWIP_ENABLE_TRACY} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

add_subdirectory(external/tracy)

add_subdirectory(tools/logger)

if(GAMEWIP_ENABLE_ASSERTS)
    set(ASSERT_ENABLED ON CACHE STRING "Force assertions on, off, or let NDEBUG choose" FORCE)
    set(ASSERT_CHECKS_ENABLED ON CACHE STRING "Force recoverable checks on, off, or let NDEBUG choose" FORCE)
endif()

add_subdirectory(tools/debug/assert)
add_subdirectory(engine/input)
add_subdirectory(engine/action)
add_subdirectory(engine/window)
add_subdirectory(engine/window_manager)

add_executable(GameWIP
    game/main.cpp
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
    Logger
    Assert
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
    COMMAND $<TARGET_FILE:GameWIP>
)
