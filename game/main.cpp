/// @file main.cpp
/// @brief GameWIP process entry point.

#include "runtime/game.h"
#include "validation/validation.h"

#include <cstdlib>

int main(int argc, char **argv)
{
    const GameWIP::Validation::TestResult tests = GameWIP::Validation::runTests(argc, argv);
    if (tests.handledChildInvocation)
    {
        return tests.exitCode;
    }
    if (!tests.ok())
    {
        return tests.exitCode == 0 ? EXIT_FAILURE : tests.exitCode;
    }

    const GameWIP::Validation::BenchmarkResult benchmarks = GameWIP::Validation::runBenchmarks(argc, argv);
    if (!benchmarks.ok())
    {
        return EXIT_FAILURE;
    }

    return GameWIP::Game::run(argc, argv);
}
