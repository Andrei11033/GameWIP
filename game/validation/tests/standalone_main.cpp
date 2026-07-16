/// @file standalone_main.cpp
/// @brief Standalone correctness-test process entry point.
///
/// This binary is the normal focused local and CI entry point for modular correctness validation.

#include "validation/tests/runner.h"

#include <cstdlib>

int main(int argc, char **argv)
{
    const GameWIP::Validation::TestResult result = GameWIP::Validation::Tests::run(argc, argv);
    // Routed child invocations preserve the module-owned protocol exit code;
    // normal aggregate runs expose only process success or failure.
    if (result.handledChildInvocation)
    {
        return result.exitCode;
    }
    return result.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
