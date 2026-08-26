/// @file main.cpp
/// @brief GameWIP process entry point and startup sequencing.
///
/// This file should remain the stable process boundary. It handles process-level
/// utility arguments, runs optional startup validation/benchmarks, and then
/// delegates runtime execution to GameWIP::Game::run().

#include "runtime/game.h"
#include "validation/process_arguments.h"
#include "validation/validation.h"

#include <gamewip/version.h>

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string_view>

namespace
{
#if GAMEWIP_TRACY_ENABLED
    namespace ProfileZoneColor
    {
        inline constexpr auto Process = 0x4C78A8;
        inline constexpr auto Validation = 0xF58518;
        inline constexpr auto Benchmark = 0xB279A2;
    } // namespace ProfileZoneColor
#endif

    /// @brief Returns whether the process was invoked only to print version metadata.
    bool requestsVersion(GameWIP::Validation::ProcessArguments arguments) noexcept
    {
        return arguments.size() == 2 && arguments[1] != nullptr && std::string_view(arguments[1]) == "--version";
    }

    /// @brief Returns whether the process was invoked only to print command-line help.
    bool requestsHelp(GameWIP::Validation::ProcessArguments arguments) noexcept
    {
        if (arguments.size() != 2 || arguments[1] == nullptr)
        {
            return false;
        }
        const std::string_view argument(arguments[1]);
        return argument == "--help" || argument == "-h" || argument == "-?";
    }

    /// @brief Prints the supported GameWIP executable command line without starting runtime services.
    void printHelp() noexcept
    {
        std::puts("Usage:");
        std::puts("  GameWIP.exe");
        std::puts("  GameWIP.exe --version");
        std::puts("  GameWIP.exe --help | -h | -?");
#if GAMEWIP_STARTUP_TESTS_ENABLED
        std::puts("  GameWIP.exe --startup-tests [GameWIPTests options]");
        std::puts("");
        std::puts("This build includes opt-in startup correctness tests.");
#else
        std::puts("");
        std::puts("This build does not include startup correctness tests.");
#endif
#if GAMEWIP_STARTUP_BENCHMARKS_ENABLED
        std::puts("This build runs embedded benchmarks and accepts --benchmark_* and --v=<level> options.");
#else
        std::puts("This build does not include startup benchmarks.");
#endif
        std::puts("Use GameWIPTests.exe --help and GameWIPBenchmarks.exe --help for validation options.");
    }
} // namespace

int main(int argc, char **argv)
{
#if GAMEWIP_TRACY_ENABLED
    tracy::SetThreadName("GameWIP Main");
    ZoneScopedNC("GameWIP process", ProfileZoneColor::Process);
#endif

    // Keep utility-only invocations independent from validation so packaging and
    // smoke-test scripts can query metadata from any build configuration.
    const GameWIP::Validation::ProcessArguments arguments = GameWIP::Validation::processArguments(argc, argv);
    if (requestsVersion(arguments))
    {
        std::cout << GameWIP::Version::productDisplay << '\n';
        return EXIT_SUCCESS;
    }
    if (requestsHelp(arguments))
    {
        printHelp();
        return EXIT_SUCCESS;
    }

    // Startup tests are allowed to consume child-process validation arguments.
    // In that case the process must return the child route result directly and
    // must not continue into benchmarks or the runtime.
    GameWIP::Validation::TestResult tests;
    if (GameWIP::Validation::shouldRunTests(argc, argv))
    {
#if GAMEWIP_TRACY_ENABLED
        FrameMark;
        ZoneScopedNC("Startup validation", ProfileZoneColor::Validation);
#endif
        tests = GameWIP::Validation::runTests(argc, argv);
    }
    if (tests.handledChildInvocation)
    {
        return tests.exitCode;
    }
    if (!tests.ok())
    {
        return tests.exitCode == 0 ? EXIT_FAILURE : tests.exitCode;
    }

    // Benchmarks run after correctness validation so startup measurements are not
    // collected from a build whose reusable-library checks already failed.
    GameWIP::Validation::BenchmarkResult benchmarks;
    {
#if GAMEWIP_TRACY_ENABLED
        FrameMark;
        ZoneScopedNC("Startup benchmarks", ProfileZoneColor::Benchmark);
#endif
        benchmarks = GameWIP::Validation::runBenchmarks(argc, argv);
    }
    if (!benchmarks.ok())
    {
        return EXIT_FAILURE;
    }

    return GameWIP::Game::run(argc, argv);
}
