/// @file runner.cpp
/// @brief Embedded and standalone Google Benchmark runner implementation.

#include "validation/benchmarks/runner.h"

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Validation::Benchmarks
{
    namespace
    {
        [[nodiscard]] bool isEmbeddedBenchmarkArgument(std::string_view argument)
        {
            return argument == "--help" || argument.starts_with("--benchmark_") || argument.starts_with("--v=");
        }
    } // namespace

    BenchmarkResult run(int argc, char **argv, bool embedded)
    {
        std::vector<std::string> arguments;
        arguments.emplace_back(argc > 0 && argv[0] != nullptr ? argv[0] : "GameWIPBenchmarks");
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && (!embedded || isEmbeddedBenchmarkArgument(argv[index])))
            {
                arguments.emplace_back(argv[index]);
            }
        }

        std::vector<char *> argumentPointers;
        argumentPointers.reserve(arguments.size());
        for (std::string &argument : arguments)
        {
            argumentPointers.push_back(argument.data());
        }

        int benchmarkArgc = static_cast<int>(argumentPointers.size());
        benchmark::Initialize(&benchmarkArgc, argumentPointers.data());
        if (benchmark::ReportUnrecognizedArguments(benchmarkArgc, argumentPointers.data()))
        {
            benchmark::Shutdown();
            return {.argumentsValid = false};
        }

        const std::size_t benchmarksRun = benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        return {.benchmarksRun = benchmarksRun};
    }
} // namespace GameWIP::Validation::Benchmarks
