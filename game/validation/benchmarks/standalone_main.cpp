/// @file standalone_main.cpp
/// @brief Standalone Google Benchmark process entry point.

#include "validation/benchmarks/runner.h"

#include <cstdlib>

int main(int argc, char **argv)
{
    const GameWIP::Validation::BenchmarkResult result = GameWIP::Validation::Benchmarks::run(argc, argv, false);
    return result.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
