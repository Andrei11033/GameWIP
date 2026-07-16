/// @file assert_benchmark.cpp
/// @brief Passing-path Google Benchmark scenarios for Assert macros.
///
/// These scenarios measure passing macro paths only; correctness and failure
/// behavior remain owned by the Assert validation module.

#include "debug/assert/assert.h"

#include <benchmark/benchmark.h>

namespace
{
    /// @brief Produces a runtime-visible passing condition that cannot be folded at compile time.
    bool passingCondition(benchmark::State &state)
    {
        return state.range(0) != 0;
    }

    /// @brief Measures the enabled passing path for ASSERT.
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

    /// @brief Measures the passing path for CHECK.
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

    /// @brief Measures the passing path for always-evaluated VERIFY.
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

    /// @brief Measures the passing path for ASSERT_INTERACTIVE.
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

    /// @brief Measures the passing path for always-evaluated VERIFY_INTERACTIVE.
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

    /// @brief Measures ENSURE while keeping its boolean result observable.
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
