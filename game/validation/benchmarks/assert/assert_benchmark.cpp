/// @file assert_benchmark.cpp
/// @brief Passing-path Google Benchmark scenarios for Assert macros.

#include "debug/assert/assert.h"

#include <benchmark/benchmark.h>

namespace
{
    bool passingCondition(benchmark::State &state)
    {
        return state.range(0) != 0;
    }

    void BM_Assert_Passing(benchmark::State &state)
    {
        bool condition = passingCondition(state);
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            benchmark::DoNotOptimize(condition);
            ASSERT(condition);
        }
    }

    void BM_Check_Passing(benchmark::State &state)
    {
        bool condition = passingCondition(state);
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            benchmark::DoNotOptimize(condition);
            CHECK(condition);
        }
    }

    void BM_Verify_Passing(benchmark::State &state)
    {
        bool condition = passingCondition(state);
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            benchmark::DoNotOptimize(condition);
            VERIFY(condition);
        }
    }

    void BM_AssertInteractive_Passing(benchmark::State &state)
    {
        bool condition = passingCondition(state);
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            benchmark::DoNotOptimize(condition);
            ASSERT_INTERACTIVE(condition);
        }
    }

    void BM_VerifyInteractive_Passing(benchmark::State &state)
    {
        bool condition = passingCondition(state);
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            benchmark::DoNotOptimize(condition);
            VERIFY_INTERACTIVE(condition);
        }
    }

    void BM_Ensure_Passing(benchmark::State &state)
    {
        bool condition = passingCondition(state);
        for (auto iteration : state)
        {
            static_cast<void>(iteration);
            benchmark::DoNotOptimize(condition);
            bool result = ENSURE(condition);
            benchmark::DoNotOptimize(result);
        }
    }

    BENCHMARK(BM_Assert_Passing)->Arg(1);
    BENCHMARK(BM_Check_Passing)->Arg(1);
    BENCHMARK(BM_Verify_Passing)->Arg(1);
    BENCHMARK(BM_AssertInteractive_Passing)->Arg(1);
    BENCHMARK(BM_VerifyInteractive_Passing)->Arg(1);
    BENCHMARK(BM_Ensure_Passing)->Arg(1);
} // namespace
