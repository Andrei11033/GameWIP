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
#endif

    if (requestsVersion(argc, argv))
    {
        std::puts(GameWIP::Version::productDisplay);
        return EXIT_SUCCESS;
    }

    GameWIP::Validation::TestResult tests;
    {
#if GAMEWIP_TRACY_ENABLED
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

    GameWIP::Validation::BenchmarkResult benchmarks;
    {
#if GAMEWIP_TRACY_ENABLED
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
