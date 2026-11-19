# Zig toolchain for Linux x64 and ARM64 targets.

list(
    APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    GAMEWIP_TARGET_COMPILER_TRIPLE
    GAMEWIP_TARGET_PROCESSOR
    GAMEWIP_ZIG_BINARY_DIRECTORY
    GAMEWIP_ZIG_COMPILER
    GAMEWIP_ZIG_ROOT
)

if(NOT GAMEWIP_TARGET_COMPILER_TRIPLE OR NOT GAMEWIP_ZIG_ROOT OR NOT GAMEWIP_ZIG_COMPILER)
    message(FATAL_ERROR "GameWIP target setup did not provide a Zig compiler and root.")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "${GAMEWIP_TARGET_PROCESSOR}")

# Resolve Zig for the current build host.
set(_gamewip_zig_bin "${GAMEWIP_ZIG_ROOT}/${GAMEWIP_ZIG_BINARY_DIRECTORY}")
if(CMAKE_HOST_WIN32)
    set(_gamewip_zig_executable "${_gamewip_zig_bin}/${GAMEWIP_ZIG_COMPILER}.exe")
else()
    set(_gamewip_zig_executable "${_gamewip_zig_bin}/${GAMEWIP_ZIG_COMPILER}")
endif()

if(NOT EXISTS "${_gamewip_zig_executable}")
    message(FATAL_ERROR "The selected Zig compiler is missing: ${_gamewip_zig_executable}")
endif()

# Keep the target triple and compiler driver consistent for C and C++.
set(CMAKE_C_COMPILER "${_gamewip_zig_executable}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_ARG1 cc CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_INIT "-target ${GAMEWIP_TARGET_COMPILER_TRIPLE}")
set(CMAKE_CXX_COMPILER "${_gamewip_zig_executable}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_ARG1 c++ CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_INIT "-target ${GAMEWIP_TARGET_COMPILER_TRIPLE}")

# Resolve programs on the build host and libraries for the selected target.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

unset(_gamewip_zig_bin)
unset(_gamewip_zig_executable)
