/// @file module.cpp
/// @brief Registers the Status correctness-test module.

#include "validation/tests/registry.h"
#include "validation/tests/status/status_test.h"

namespace
{
    /// @brief Maps shared runner policy to Status test options and runs the checks.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::StatusTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;

        return GameWIP::Test::runStatusTests(invocation.argc, invocation.argv, options);
    }

    // ------------------------------------------------------------
    // Module registration
    // ------------------------------------------------------------

    const GameWIP::Validation::Tests::Registration kRegistration({.name = "status", .order = 7, .run = run});
} // namespace
