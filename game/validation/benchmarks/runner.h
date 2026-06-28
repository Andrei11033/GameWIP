/// @file runner.h
/// @brief Embedded and standalone Google Benchmark runner.

#pragma once

#include "validation/types.h"

namespace GameWIP::Validation::Benchmarks
{
    [[nodiscard]] BenchmarkResult run(int argc, char **argv, bool embedded);
} // namespace GameWIP::Validation::Benchmarks
