/// @file assert_benchmark.cpp
/// @brief Google Benchmark scenarios for the Assert library.

#include "debug/assert/assert.h"

#include <benchmark/benchmark.h>

namespace
{
    void BM_EnsurePassing(benchmark::State &state)
    {
        bool condition = state.range(0) != 0;

        for (auto _ : state)
        {
            benchmark::DoNotOptimize(condition);
            bool result = ENSURE(condition);
            benchmark::DoNotOptimize(result);
        }
    }

    BENCHMARK(BM_EnsurePassing)->Arg(1);
} // namespace