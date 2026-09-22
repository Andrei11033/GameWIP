# Top-level builds favor a complete workflow.
# Embedded builds include only the reusable GameWIP library by default.

if(PROJECT_IS_TOP_LEVEL)
    set(_gamewip_default_build_game ON)
    set(_gamewip_default_build_tests ON)
    set(_gamewip_default_build_benchmarks OFF)
    set(_gamewip_default_startup_tests OFF)
    set(_gamewip_default_startup_benchmarks OFF)
    set(_gamewip_default_enable_tracy ON)
    set(_gamewip_default_static_analysis OFF)
    set(_gamewip_default_build_docs OFF)
    set(_gamewip_default_install_docs OFF)
else()
    set(_gamewip_default_build_game OFF)
    set(_gamewip_default_build_tests OFF)
    set(_gamewip_default_build_benchmarks OFF)
    set(_gamewip_default_startup_tests OFF)
    set(_gamewip_default_startup_benchmarks OFF)
    set(_gamewip_default_enable_tracy OFF)
    set(_gamewip_default_static_analysis OFF)
    set(_gamewip_default_build_docs OFF)
    set(_gamewip_default_install_docs OFF)
endif()

option(GAMEWIP_BUILD_GAME "Build the GameWIP runtime executable" ${_gamewip_default_build_game})

option(GAMEWIP_BUILD_TESTS "Build the standalone GameWIPTests executable" ${_gamewip_default_build_tests})

option(GAMEWIP_BUILD_BENCHMARKS "Build the standalone GameWIPBenchmarks executable" ${_gamewip_default_build_benchmarks})

option(GAMEWIP_ENABLE_STARTUP_TESTS "Compile correctness tests into the game for opt-in --startup-tests execution" ${_gamewip_default_startup_tests})

option(GAMEWIP_RUN_BENCHMARKS_AT_STARTUP "Compile and run benchmarks before game startup" ${_gamewip_default_startup_benchmarks})

option(GAMEWIP_ENABLE_TRACY "Enable Tracy profiler instrumentation" ${_gamewip_default_enable_tracy})

option(GAMEWIP_WARNINGS_AS_ERRORS "Treat GameWIP compiler warnings as build errors" OFF)

option(GAMEWIP_ENABLE_ASSERTS "Enable assertions and recoverable checks" ON)

option(GAMEWIP_ENABLE_COVERAGE "Enable coverage instrumentation for validation builds" OFF)

option(GAMEWIP_ENABLE_ADDRESS_SANITIZER "Enable AddressSanitizer instrumentation" OFF)

option(GAMEWIP_ENABLE_UNDEFINED_BEHAVIOR_SANITIZER "Enable UndefinedBehaviorSanitizer instrumentation" OFF)

option(GAMEWIP_ENABLE_STATIC_ANALYSIS "Create clang-tidy and clang-format validation targets" ${_gamewip_default_static_analysis})

option(GAMEWIP_BUILD_DOCS "Build project Doxygen documentation" ${_gamewip_default_build_docs})

option(GAMEWIP_INSTALL_DOCS "Install generated Doxygen HTML documentation" ${_gamewip_default_install_docs})

unset(_gamewip_default_build_game)
unset(_gamewip_default_build_tests)
unset(_gamewip_default_build_benchmarks)
unset(_gamewip_default_startup_tests)
unset(_gamewip_default_startup_benchmarks)
unset(_gamewip_default_enable_tracy)
unset(_gamewip_default_static_analysis)
unset(_gamewip_default_build_docs)
unset(_gamewip_default_install_docs)

if(NOT GAMEWIP_BUILD_GAME AND (GAMEWIP_ENABLE_STARTUP_TESTS OR GAMEWIP_RUN_BENCHMARKS_AT_STARTUP))
    message(FATAL_ERROR "Startup validation requires GAMEWIP_BUILD_GAME=ON. " "Disable the startup option or build the game executable.")
endif()

if(GAMEWIP_INSTALL_DOCS AND NOT GAMEWIP_BUILD_DOCS)
    message(FATAL_ERROR "GAMEWIP_INSTALL_DOCS requires GAMEWIP_BUILD_DOCS=ON.")
endif()

if(GAMEWIP_ENABLE_COVERAGE AND NOT GAMEWIP_BUILD_TESTS)
    message(FATAL_ERROR "GAMEWIP_ENABLE_COVERAGE requires GAMEWIP_BUILD_TESTS=ON.")
endif()

# Derived requirements ensure validation modules exist whenever they are needed
# by standalone or startup test and benchmark runners.

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
