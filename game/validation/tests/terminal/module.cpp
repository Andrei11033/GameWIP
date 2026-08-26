/// @file module.cpp
/// @brief Registers the Terminal correctness-test module with the validation runner.

#include "validation/tests/terminal/terminal_test.h"

#include "validation/process_arguments.h"
#include "validation/tests/registry.h"

#include <algorithm>
#include <string_view>

namespace
{
    /// @brief Detects isolated child invocations owned by Terminal tests.
    bool handlesChildArguments(int argc, char **argv)
    {
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);
        for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if (value != nullptr && (std::string_view(value) == "--terminal-test-child=reentrant-format" ||
                                     std::string_view(value) == "--terminal-test-child=session-reentrant-format"))
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
