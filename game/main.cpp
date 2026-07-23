/// @file main.cpp
/// @brief GameWIP process entry point and startup sequencing.
///
/// This file should remain the stable process boundary. It handles process-level
/// utility arguments, runs optional startup validation/benchmarks, and then
/// delegates runtime execution to GameWIP::Game::run().

#include "runtime/game.h"
#include "validation/validation.h"

#include <gamewip/version.h>

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <cstdlib>
#include <cstdio>
#include <string_view>

#if GAMEWIP_TRACY_ENABLED
#include <chrono>
#include <thread>
#endif

namespace
{
    /// @brief Returns whether the process was invoked only to print version metadata.
    bool requestsVersion(int argc, char **argv) noexcept
    {
        return argc == 2 && argv != nullptr && argv[1] != nullptr && std::string_view(argv[1]) == "--version";
    }
} // namespace

int main(int argc, char **argv)
{
#if GAMEWIP_TRACY_ENABLED
    tracy::SetThreadName("GameWIP Main");
    ZoneScopedN("GameWIP process");
    {
        ZoneScopedN("Tracy startup wait");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
#endif

    // Keep utility-only invocations independent from validation so packaging and
    // smoke-test scripts can query metadata from any build configuration.
    if (requestsVersion(argc, argv))
    {
        std::puts(GameWIP::Version::productDisplay);
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
        ZoneScopedN("Startup validation");
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
        ZoneScopedN("Startup benchmarks");
#endif
        benchmarks = GameWIP::Validation::runBenchmarks(argc, argv);
    }
    if (!benchmarks.ok())
    {
        return EXIT_FAILURE;
    }

    return GameWIP::Game::run(argc, argv);
}
