/// @file module.cpp
/// @brief Registers the Window correctness-test module.

#include "validation/tests/window/window_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::WindowTestOptions options;
        options.enableManualTests = invocation.options.enableManualTests;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runWindowTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "window",
        .order = 35,
        .run = run,
    });
} // namespace
