/// @file module.cpp
/// @brief Registers the IO correctness-test module with the validation runner.

#include "validation/tests/io/io_test.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner policy to IO-specific test options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::IOTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runIOTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic IO module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "io",
        .order = 10,
        .run = run,
    });
} // namespace
