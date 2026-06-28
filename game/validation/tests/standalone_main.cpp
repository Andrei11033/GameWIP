/// @file standalone_main.cpp
/// @brief Standalone correctness-test process entry point.

#include "validation/tests/runner.h"

#include <cstdlib>

int main(int argc, char **argv)
{
    const GameWIP::Validation::TestResult result = GameWIP::Validation::Tests::run(argc, argv);
    if (result.handledChildInvocation)
    {
        return result.exitCode;
    }
    return result.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
