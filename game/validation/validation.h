/// @file validation.h
/// @brief Compile-time facade for optional startup validation.

#pragma once

#include "validation/types.h"

#ifndef GAMEWIP_STARTUP_TESTS_ENABLED
#define GAMEWIP_STARTUP_TESTS_ENABLED 0
#endif

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
