/// @file main.cpp
/// @brief GameWIP process entry point.

#include "runtime/game.h"
#include "validation/validation.h"

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <cstdlib>

int main(int argc, char **argv)
{
#if GAMEWIP_TRACY_ENABLED
    tracy::SetThreadName("GameWIP Main");
    ZoneScopedN("GameWIP process");
#endif

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
