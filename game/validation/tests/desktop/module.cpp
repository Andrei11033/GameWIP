/// @file module.cpp
/// @brief Registers the Desktop correctness-test module.

#include "validation/tests/desktop/desktop_test.h"
#include "validation/tests/desktop/platform.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Delegates native child-mode ownership to the selected backend.
    bool handlesChildArguments(int argc, char **argv)
    {
        return GameWIP::Test::DesktopPlatform::handlesChildArguments(argc, argv);
    }

    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::DesktopTestOptions options;
        options.enableManualTests = invocation.options.enableManualTests;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runDesktopTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration kRegistration({
        .name = "desktop",
        .order = 35,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
