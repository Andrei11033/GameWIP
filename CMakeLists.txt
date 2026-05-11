cmake_minimum_required(VERSION 3.20)

project(GameWIP VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to enable Tracy profiler instrumentation.
option(GAMEWIP_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)
option(GAMEWIP_ENABLE_TOOLS "Enable editor/tool window support in the game executable" OFF)
option(GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP "Open tool/debug windows at startup when tool support is enabled" OFF)

set(TRACY_ENABLE ${GAMEWIP_ENABLE_TRACY} CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

add_subdirectory(external/tracy)

# Build the logger as a DLL so it can stay separate from the game executable.
add_library(Logger SHARED
    tools/logger/logger.cpp
)

add_library(Assert STATIC
    engine/debug/assert/assert.cpp
)

add_library(Input STATIC
    engine/input/input.cpp
    engine/input/platform/win32/win32_input.cpp
)

add_library(PlatformWin32 STATIC
    engine/platform/win32/window.cpp
    engine/platform/win32/window_system.cpp
)

# Public include path because anything linking Logger needs logger/logger.h.
target_include_directories(Logger PUBLIC
    tools
)

target_include_directories(Assert PUBLIC
    engine
    tools
)

target_include_directories(Input PUBLIC
    engine
)

target_include_directories(PlatformWin32 PUBLIC
    engine
)

target_compile_features(Logger PUBLIC cxx_std_20)

target_compile_features(Assert PUBLIC cxx_std_20)

target_compile_features(Input PUBLIC cxx_std_20)

target_compile_features(PlatformWin32 PUBLIC cxx_std_20)

target_compile_definitions(Logger PRIVATE GAMEWIP_LOGGER_BUILD)

add_executable(GameWIP
    game/main.cpp
)

if(GAMEWIP_ENABLE_TOOLS)
    target_compile_definitions(GameWIP PRIVATE GAMEWIP_ENABLE_TOOLS=1)
    if(GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP)
        target_compile_definitions(GameWIP PRIVATE GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP=1)
    endif()
endif()

# Link the game executable against the logger DLL/import library.
target_link_libraries(Assert PUBLIC
    Logger
)

target_link_libraries(PlatformWin32 PUBLIC
    Input
)

target_link_libraries(PlatformWin32 PRIVATE
    shell32
    Tracy::TracyClient
)

target_link_libraries(GameWIP PRIVATE
    Logger
    Assert
    Input
    PlatformWin32
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
