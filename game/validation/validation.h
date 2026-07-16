/// @file validation.h
/// @brief Compile-time facade for optional validation embedded in the GameWIP executable.
///
/// main.cpp includes this header unconditionally. When startup tests or
/// benchmarks are disabled, the inline functions become lightweight no-ops so
/// the executable does not retain validation runner dependencies.

#pragma once

#include "validation/types.h"

/// @def GAMEWIP_STARTUP_TESTS_ENABLED
/// @brief Target-private switch selecting the real startup correctness runner.
#ifndef GAMEWIP_STARTUP_TESTS_ENABLED
#define GAMEWIP_STARTUP_TESTS_ENABLED 0
#endif

/// @def GAMEWIP_STARTUP_BENCHMARKS_ENABLED
/// @brief Target-private switch selecting the real startup benchmark runner.
#ifndef GAMEWIP_STARTUP_BENCHMARKS_ENABLED
#define GAMEWIP_STARTUP_BENCHMARKS_ENABLED 0
#endif

#if GAMEWIP_STARTUP_TESTS_ENABLED
#include "validation/tests/runner.h"
#endif

#if GAMEWIP_STARTUP_BENCHMARKS_ENABLED
#include "validation/benchmarks/runner.h"
#endif

namespace GameWIP::Validation
{
    /// @brief Returns whether this invocation must enter the embedded correctness-test runner.
    [[nodiscard]] inline bool shouldRunTests(int argc, char **argv) noexcept
    {
#if GAMEWIP_STARTUP_TESTS_ENABLED
        return Tests::requestsRun(argc, argv);
#else
        static_cast<void>(argc);
        static_cast<void>(argv);
        return false;
#endif
    }

    /// @brief Runs startup correctness validation when it was compiled into GameWIP.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process argument values forwarded unchanged to the runner.
    /// @return Runner result, or a successful empty result when startup tests are disabled.
    /// @note A result with handledChildInvocation set must be returned directly by the process entry point.
    [[nodiscard]] inline TestResult runTests(int argc, char **argv)
    {
#if GAMEWIP_STARTUP_TESTS_ENABLED
        return Tests::run(argc, argv);
#else
        static_cast<void>(argc);
        static_cast<void>(argv);
        return {};
#endif
    }

    /// @brief Runs startup benchmarks when they were compiled into GameWIP.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process argument values; only benchmark-owned arguments are forwarded in embedded mode.
    /// @return Runner result, or a successful empty result when startup benchmarks are disabled.
    [[nodiscard]] inline BenchmarkResult runBenchmarks(int argc, char **argv)
    {
#if GAMEWIP_STARTUP_BENCHMARKS_ENABLED
        return Benchmarks::run(argc, argv, true);
#else
        static_cast<void>(argc);
        static_cast<void>(argv);
        return {};
#endif
    }
} // namespace GameWIP::Validation
