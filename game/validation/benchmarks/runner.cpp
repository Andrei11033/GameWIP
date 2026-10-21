/// @file runner.cpp
/// @brief Embedded and standalone Google Benchmark runner implementation.

#include "validation/benchmarks/runner.h"
#include "validation/process_arguments.h"
#include "validation/executable_path.h"

#include "test_support/process.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace GameWIP::Validation::Benchmarks
{
    namespace
    {
        /// @brief Identifies Google Benchmark arguments that may be forwarded from the game command line.
        [[nodiscard]] bool isEmbeddedBenchmarkArgument(std::string_view argument)
        {
            return argument == "--help" || argument.starts_with("--benchmark_") || argument.starts_with("--v=");
        }

    } // namespace

    BenchmarkResult run(int argc, char **argv, bool embedded)
    {
        // Embedded startup runs share GameWIP's command line, so only Google
        // Benchmark-owned switches are forwarded to benchmark::Initialize().
        const ProcessArguments processArgumentValues = processArguments(argc, argv);
        std::filesystem::path temporaryRoot;

        try
        {
            temporaryRoot = currentExecutablePath().parent_path() / "temp";
        }
        catch (const std::system_error &error)
        {
            std::cerr << "Benchmark executable path discovery failed: " << error.what() << '\n';
            return {.argumentsValid = false};
        }
        std::error_code temporaryError;
        std::filesystem::create_directories(temporaryRoot, temporaryError);

        if (temporaryError)
        {
            std::cerr << "Benchmark temporary directory setup failed: " << temporaryError.message() << '\n';
            return {.argumentsValid = false};
        }
        const std::u8string temporaryRootUtf8 = temporaryRoot.u8string();
        const std::string temporaryRootText(temporaryRootUtf8.begin(), temporaryRootUtf8.end());
        TestSupport::ScopedProcessState processState;

        if (!processState.status().ok())
        {
            std::cerr << "Benchmark process-state setup failed.\n";
            return {.argumentsValid = false};
        }
        TestSupport::ScopedEnvironmentVariable temporaryDirectory(processState, "TEMP", temporaryRootText);
        TestSupport::ScopedEnvironmentVariable temporaryDirectoryAlias(processState, "TMP", temporaryRootText);
        TestSupport::ScopedEnvironmentVariable posixTemporaryDirectory(processState, "TMPDIR", temporaryRootText);

        if (!temporaryDirectory.status().ok() || !temporaryDirectoryAlias.status().ok() || !posixTemporaryDirectory.status().ok())
        {
            std::cerr << "Benchmark temporary environment setup failed.\n";
            return {.argumentsValid = false};
        }

        std::vector<std::string> arguments;
        arguments.emplace_back(
            !processArgumentValues.empty() && processArgumentValues.front() != nullptr ? processArgumentValues.front() : "GameWIPBenchmarks");

        for (char *value : processArgumentValues.subspan(std::min<std::size_t>(1, processArgumentValues.size())))
        {
            if (value != nullptr && (!embedded || isEmbeddedBenchmarkArgument(value)))
            {
                arguments.emplace_back(value);
            }
        }

        // Keep the strings alive while exposing mutable argv-style pointers;
        // benchmark::Initialize() follows the traditional int*/char** contract.
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
