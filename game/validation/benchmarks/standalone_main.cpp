/// @file standalone_main.cpp
/// @brief Standalone Google Benchmark process entry point.
///
/// This binary is the normal entry point for benchmark registration checks and benchmark measurement runs.

#include "validation/benchmarks/runner.h"

#include <cstdlib>

int main(int argc, char **argv)
{
    // The runner result reflects Google Benchmark argument validity; individual
    // scenario diagnostics remain in Google Benchmark's own output.
    const GameWIP::Validation::BenchmarkResult result = GameWIP::Validation::Benchmarks::run(argc, argv, false);
    return result.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
