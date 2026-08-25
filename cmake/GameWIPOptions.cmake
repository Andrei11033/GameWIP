# Owns repository-wide first-party CMake options and their ordinary local defaults.
# CI/presets may override these values; this file does not own external dependency options.

option(GAMEWIP_BUILD_GAME "Build the GameWIP runtime executable" ON)
option(GAMEWIP_BUILD_TESTS "Build the standalone GameWIPTests executable" ON)
option(GAMEWIP_BUILD_BENCHMARKS "Build the standalone GameWIPBenchmarks executable" OFF)
option(GAMEWIP_WARNINGS_AS_ERRORS "Treat GameWIP compiler warnings as build errors" OFF)
option(GAMEWIP_ENABLE_UNSAFE_BUFFER_WARNINGS "Enable Clang's experimental unsafe-buffer migration diagnostics" OFF)
option(GAMEWIP_ENABLE_STARTUP_TESTS "Compile correctness tests into the game for opt-in --startup-tests execution" OFF)
option(GAMEWIP_RUN_BENCHMARKS_AT_STARTUP "Compile and run benchmarks before game startup" OFF)

option(GAMEWIP_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(GAMEWIP_ENABLE_ASSERTS "Enable assertions and recoverable checks" ON)
option(GAMEWIP_ENABLE_COVERAGE "Enable coverage instrumentation for validation builds" OFF)
option(GAMEWIP_ENABLE_ADDRESS_SANITIZER "Enable AddressSanitizer instrumentation" OFF)
option(GAMEWIP_ENABLE_STATIC_ANALYSIS "Create clang-tidy and clang-format validation targets" OFF)
option(GAMEWIP_BUILD_DOCS "Build project Doxygen documentation" OFF)
option(GAMEWIP_INSTALL_DOCS "Install generated Doxygen HTML documentation" OFF)

set(GAMEWIP_CLANG_TIDY_JOBS "4" CACHE STRING "Parallel clang-tidy process count")

if(NOT GAMEWIP_BUILD_GAME AND (GAMEWIP_ENABLE_STARTUP_TESTS OR GAMEWIP_RUN_BENCHMARKS_AT_STARTUP))
    message(FATAL_ERROR "Startup validation requires GAMEWIP_BUILD_GAME=ON. Disable the startup option or build the game executable.")
endif()

if(GAMEWIP_INSTALL_DOCS AND NOT GAMEWIP_BUILD_DOCS)
    message(FATAL_ERROR "GAMEWIP_INSTALL_DOCS requires GAMEWIP_BUILD_DOCS=ON.")
endif()

if(GAMEWIP_ENABLE_COVERAGE AND NOT GAMEWIP_BUILD_TESTS)
    message(FATAL_ERROR "GAMEWIP_ENABLE_COVERAGE requires GAMEWIP_BUILD_TESTS=ON.")
endif()

if(GAMEWIP_BUILD_TESTS OR GAMEWIP_ENABLE_STARTUP_TESTS)
    set(GAMEWIP_TESTS_REQUIRED ON)
else()
    set(GAMEWIP_TESTS_REQUIRED OFF)
endif()

if(GAMEWIP_BUILD_BENCHMARKS OR GAMEWIP_RUN_BENCHMARKS_AT_STARTUP)
    set(GAMEWIP_BENCHMARKS_REQUIRED ON)
else()
    set(GAMEWIP_BENCHMARKS_REQUIRED OFF)
endif()
