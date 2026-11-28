/// @file main.cpp
/// @brief GameWIP process entry point and startup sequencing.
///
/// The process entry point handles utility arguments and optional startup
/// validation or benchmark runs before handing runtime execution to
/// GameWIP::Game::run(). Runtime composition lives in runtime/game.cpp.

#include "runtime/game.h"
#include "runtime/profiling.h"
#include "validation/process_arguments.h"
#include "validation/validation.h"

#include <gamewip/version.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <string_view>
#include <vector>

#ifndef GAMEWIP_STANDALONE_BENCHMARKS_ENABLED
#define GAMEWIP_STANDALONE_BENCHMARKS_ENABLED 0
#endif

namespace
{
    /// ------------------------------------------------------------
    /// Argument routing
    /// ------------------------------------------------------------

    /// @brief Returns whether the process was invoked only to print version metadata.
    bool requestsVersion(GameWIP::Validation::ProcessArguments arguments) noexcept
    {
        return arguments.size() == 2 && arguments[1] != nullptr && std::string_view(arguments[1]) == "--version";
    }

    /// @brief Returns whether the process was invoked only to print command-line help.
    bool requestsHelp(GameWIP::Validation::ProcessArguments arguments) noexcept
    {
        if(arguments.size() != 2 || arguments[1] == nullptr)
        {
            return false;
        }
        const std::string_view argument(arguments[1]);
        return argument == "--help" || argument == "-h" || argument == "-?";
    }

    /// @brief Returns whether an argument belongs to runtime rather than startup tools.
    [[nodiscard]] bool isRuntimeOption(std::string_view argument) noexcept
    {
        return argument == "--logLevel" || argument.starts_with("--logLevel=") || argument == "--inputDevices" ||
               argument.starts_with("--inputDevices=") || argument == "--inputDebug" || argument.starts_with("--inputDebug=") ||
               argument == "--windowEvents" || argument.starts_with("--windowEvents=") || argument == "--inputBackends" ||
               argument.starts_with("--inputBackends=");
    }

    /// @brief Removes runtime-owned arguments before forwarding to startup validation and benchmarks.
    [[nodiscard]] std::vector<char *> argumentsForStartupTools(int argc, char *const *argv)
    {
        std::vector<char *> filtered;
        if(argc <= 0 || argv == nullptr)
        {
            return filtered;
        }

        filtered.reserve(static_cast<std::size_t>(argc));
        bool executableArgument = true;
        for(char *argument : std::views::counted(argv, argc))
        {
            if(!executableArgument && argument != nullptr && isRuntimeOption(argument))
            {
                executableArgument = false;
                continue;
            }
            filtered.push_back(argument);
            executableArgument = false;
        }
        return filtered;
    }

    /// ------------------------------------------------------------
    /// Command-line help
    /// ------------------------------------------------------------

    /// @brief Prints the supported GameWIP executable command line without starting runtime services.
    void printHelp() noexcept
    {
        std::puts("Usage:");
        std::puts("  GameWIP");
        std::puts("  GameWIP --version");
        std::puts("  GameWIP --help | -h | -?");
        std::puts("  GameWIP --logLevel=<trace|debug|info|warn|error|fatal>");
        std::puts("  GameWIP --inputDevices");
        std::puts("  GameWIP --inputDebug");
        std::puts("  GameWIP --inputBackends=<list>");
        std::puts("    Backends: rawInput, hid, xinput, gameInput, directInput, windowMessages, or none.");
        std::puts("  GameWIP --windowEvents");
#if GAMEWIP_STARTUP_TESTS_ENABLED
        std::puts("  GameWIP --startup-tests [GameWIPTests options]");
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
        std::puts("When built, use GameWIPTests --help for validation options.");
#if GAMEWIP_STANDALONE_BENCHMARKS_ENABLED
        std::puts("Use GameWIPBenchmarks --help for standalone benchmark options.");
#endif
    }
} // namespace

int main(int argc, char **argv)
{
    GAMEWIP_PROFILE_THREAD_NAME("GameWIP Main");
    GAMEWIP_PROFILE_ZONE("GameWIP process", GameWIP::Profiling::Color::kProcess);

    // Keep utility-only invocations independent from validation so packaging and
    // smoke-test scripts can query metadata from any build configuration.
    const GameWIP::Validation::ProcessArguments arguments = GameWIP::Validation::processArguments(argc, argv);

    if(requestsVersion(arguments))
    {
        std::cout << GameWIP::Version::productDisplay << '\n';
        return EXIT_SUCCESS;
    }

    if(requestsHelp(arguments))
    {
        printHelp();
        return EXIT_SUCCESS;
    }

    // Runtime-owned switches must not be mistaken for validation or benchmark arguments.
    std::vector<char *> startupArguments = argumentsForStartupTools(argc, argv);
    const int startupArgumentCount = static_cast<int>(startupArguments.size());
    char **startupArgumentValues = startupArguments.data();

    // Startup tests are allowed to consume child-process validation arguments.
    // In that case the process must return the child route result directly and
    // must not continue into benchmarks or the runtime.
    GameWIP::Validation::TestResult tests;

    if(GameWIP::Validation::shouldRunTests(startupArgumentCount, startupArgumentValues))
    {
        GameWIP::Profiling::frameMark();
        GAMEWIP_PROFILE_ZONE("Startup validation", GameWIP::Profiling::Color::kValidation);
        tests = GameWIP::Validation::runTests(startupArgumentCount, startupArgumentValues);
    }

    if(tests.handledChildInvocation)
    {
        return tests.exitCode;
    }

    if(!tests.ok())
    {
        return tests.exitCode == 0 ? EXIT_FAILURE : tests.exitCode;
    }

    // Benchmarks run after correctness validation so startup measurements are not
    // collected from a build whose reusable-library checks already failed.
    GameWIP::Validation::BenchmarkResult benchmarks;
    {
        GameWIP::Profiling::frameMark();
        GAMEWIP_PROFILE_ZONE("Startup benchmarks", GameWIP::Profiling::Color::kBenchmark);
        benchmarks = GameWIP::Validation::runBenchmarks(startupArgumentCount, startupArgumentValues);
    }

    if(!benchmarks.ok())
    {
        return EXIT_FAILURE;
    }

    return GameWIP::Game::run(argc, argv);
}
