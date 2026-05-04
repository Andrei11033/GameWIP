cmake_minimum_required(VERSION 3.20)

project(GameWIP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Link the game executable against the logger DLL/import library.
target_link_libraries(Assert PUBLIC
    Logger
)

target_link_libraries(PlatformWin32 PUBLIC
    Input
)

target_link_libraries(GameWIP PRIVATE
    Logger
    Assert
    Input
    PlatformWin32
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
