/// @file module.cpp
/// @brief Registers the Input correctness-test module with the validation runner.

#include "validation/tests/input/input_test.h"
#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner policy to Input-specific test options.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::InputTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runInputTests(options);
    }

    const GameWIP::Validation::Tests::Registration kRegistration({
        .name = "input",
        .order = 25,
        .run = run,
    });
} // namespace
