/// @file runner.h
/// @brief Google Benchmark runner used by standalone and optional startup benchmark execution.
///
/// The runner forwards only benchmark-owned command-line arguments when it is
/// embedded in the GameWIP executable.

#pragma once

#include "validation/types.h"

namespace GameWIP::Validation::Benchmarks
{
    /// @brief Initializes Google Benchmark, runs selected scenarios, and shuts it down.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param embedded When true, forwards only benchmark-owned arguments from the game command line.
    /// @return Benchmark count and command-line validation outcome.
    [[nodiscard]] BenchmarkResult run(int argc, char **argv, bool embedded);
} // namespace GameWIP::Validation::Benchmarks
