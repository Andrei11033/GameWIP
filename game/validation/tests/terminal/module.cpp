/// @file module.cpp
/// @brief Registers the Terminal correctness-test module with the validation runner.

#include "validation/tests/terminal/terminal_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
    /// @brief Detects isolated child invocations owned by Terminal tests.
    bool handlesChildArguments(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && (std::string_view(argv[index]) == "--terminal-test-child=reentrant-format" ||
                                           std::string_view(argv[index]) == "--terminal-test-child=session-reentrant-format"))
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Maps shared runner policy to Terminal-specific test options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::TerminalTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.enableManualTests = invocation.options.enableManualTests;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runTerminalTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic Terminal module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "terminal",
        .order = 30,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
