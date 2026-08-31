/// @file module.cpp
/// @brief Registers the Desktop correctness-test module.

#include "validation/tests/desktop/desktop_test.h"

#include "validation/process_arguments.h"
#include "validation/tests/registry.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

namespace
{
    /// @brief Claims only exact Desktop process-shutdown child protocols.
    bool handlesChildArguments(int argc, char **argv)
    {
        constexpr std::array selectors{
            std::string_view{"--desktop-test-child=standalone-color-shutdown"},
            std::string_view{"--desktop-test-child=window-color-shutdown"},
            std::string_view{"--desktop-test-child=owner-exit-color-shutdown"},
        };
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);
        return std::ranges::any_of(
            arguments.subspan(std::min<std::size_t>(1, arguments.size())),
            [&](const char *value)
            {
                return value != nullptr && std::ranges::find(selectors, std::string_view(value)) != selectors.end();
            });
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

    const GameWIP::Validation::Tests::Registration registration({
        .name = "desktop",
        .order = 35,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
