/// @file runner.h
/// @brief Google Benchmark runner used by standalone and optional startup benchmark execution.
///
/// The runner forwards only benchmark-owned command-line arguments when it is
/// embedded in the GameWIP executable.

#pragma once

#include "validation/types.h"

/// @brief Source-tree Google Benchmark integration for standalone and embedded execution.
namespace GameWIP::Validation::Benchmarks
{
    /// @brief Performs one Google Benchmark initialization, execution, and shutdown lifecycle.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process argument values.
    /// @param embedded When true, forwards only `--help`, `--benchmark_*`, and `--v=` arguments.
    /// @return Selected benchmark count and runner-level argument validity.
    /// @throws Any allocation exception or exception that escapes Google Benchmark initialization or execution.
    /// @note The result does not encode performance thresholds or every scenario-level `SkipWithError()` diagnostic.
    /// @note Google Benchmark owns process-global state; invoke this runner once at a time.
    [[nodiscard]] BenchmarkResult run(int argc, char **argv, bool embedded);
} // namespace GameWIP::Validation::Benchmarks
