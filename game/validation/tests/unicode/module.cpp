/// @file module.cpp
/// @brief Registers the Unicode correctness-test module with the validation runner.

#include "validation/tests/unicode/unicode_test.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner policy to Unicode-specific test options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::UnicodeTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runUnicodeTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic Unicode module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "unicode",
        .order = 15,
        .run = run,
    });
} // namespace
