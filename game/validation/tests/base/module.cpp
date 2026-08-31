/// @file module.cpp
/// @brief Registers the internal Base correctness-test module.

#include "validation/tests/base/base_test.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner report policy into the internal Base test suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::BaseTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runBaseTests(options);
    }

    const GameWIP::Validation::Tests::Registration registration({.name = "base", .order = 5, .run = run});
} // namespace
