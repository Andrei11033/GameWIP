option(GAMEWIP_BUILD_GAME "Build the GameWIP runtime executable" ON)
option(GAMEWIP_BUILD_TESTS "Build the standalone GameWIPTests executable" ON)
option(GAMEWIP_BUILD_BENCHMARKS "Build the standalone GameWIPBenchmarks executable" OFF)
option(GAMEWIP_RUN_TESTS_AT_STARTUP "Compile and run correctness tests before game startup" ON)
option(GAMEWIP_RUN_BENCHMARKS_AT_STARTUP "Compile and run benchmarks before game startup" OFF)

option(GAMEWIP_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)
option(GAMEWIP_ENABLE_TOOLS "Enable editor and tool-window support in the game executable" OFF)
option(GAMEWIP_OPEN_TOOLS_AT_STARTUP "Open tool windows when tool support is enabled" OFF)
option(GAMEWIP_ENABLE_ASSERTS "Enable assertions and recoverable checks" ON)
option(GAMEWIP_ENABLE_COVERAGE "Enable coverage instrumentation for validation builds" OFF)
option(GAMEWIP_ENABLE_ADDRESS_SANITIZER "Enable AddressSanitizer instrumentation" OFF)
option(GAMEWIP_ENABLE_STATIC_ANALYSIS "Create clang-tidy and clang-format validation targets" OFF)
option(GAMEWIP_BUILD_DOCS "Build project Doxygen documentation" OFF)
option(GAMEWIP_INSTALL_DOCS "Install generated Doxygen HTML documentation" OFF)

set(GAMEWIP_CLANG_TIDY_JOBS "4" CACHE STRING "Parallel clang-tidy process count")

if(NOT GAMEWIP_BUILD_GAME AND (GAMEWIP_RUN_TESTS_AT_STARTUP OR GAMEWIP_RUN_BENCHMARKS_AT_STARTUP))
    message(FATAL_ERROR
        "Startup validation requires GAMEWIP_BUILD_GAME=ON. Disable the startup option or build the game executable."
    )
endif()

if(GAMEWIP_INSTALL_DOCS AND NOT GAMEWIP_BUILD_DOCS)
    message(FATAL_ERROR "GAMEWIP_INSTALL_DOCS requires GAMEWIP_BUILD_DOCS=ON.")
endif()

if(GAMEWIP_ENABLE_COVERAGE AND NOT GAMEWIP_BUILD_TESTS)
    message(FATAL_ERROR "GAMEWIP_ENABLE_COVERAGE requires GAMEWIP_BUILD_TESTS=ON.")
endif()

if(GAMEWIP_BUILD_TESTS OR GAMEWIP_RUN_TESTS_AT_STARTUP)
    set(GAMEWIP_TESTS_REQUIRED ON)
else()
    set(GAMEWIP_TESTS_REQUIRED OFF)
endif()

if(GAMEWIP_BUILD_BENCHMARKS OR GAMEWIP_RUN_BENCHMARKS_AT_STARTUP)
    set(GAMEWIP_BENCHMARKS_REQUIRED ON)
else()
    set(GAMEWIP_BENCHMARKS_REQUIRED OFF)
endif()
