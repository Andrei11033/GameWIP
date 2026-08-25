/// @file standalone_main.cpp
/// @brief Standalone correctness-test process entry point.
///
/// This binary is the normal focused local and CI entry point for modular correctness validation.

#include "validation/tests/runner.h"

#include "validation/process_arguments.h"
#include "validation/tests/registry.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
    /// @brief Returns whether the standalone runner was invoked only for help.
    [[nodiscard]] bool requestsHelp(GameWIP::Validation::ProcessArguments arguments) noexcept
    {
        if (arguments.size() != 2 || arguments[1] == nullptr)
        {
            return false;
        }
        const std::string_view argument(arguments[1]);
        return argument == "--help" || argument == "-h" || argument == "-?";
    }

    /// @brief Prints public runner options and registered modules without executing validation.
    void printHelp() noexcept
    {
        std::puts("Usage:");
        std::puts("  GameWIPTests.exe [options]");
        std::puts("");
        std::puts("Options:");
        std::puts("  --help, -h, -?                       Print help without running tests.");
        std::puts("  --test-module=<name>                 Run one registered module.");
        std::puts("  --skip-test-module=<name>            Skip one module; may be repeated.");
        std::puts("  --test-report=<path>                 Select the aggregate report path.");
        std::puts("  --no-test-report                     Disable retained report output.");
        std::puts("  --verbose-tests                      Mirror complete suite output to stdout.");
        std::puts("  --manual-tests                       Enable human-interactive checks.");
        std::puts("  --no-test-support-child-process      Disable TestSupport process checks.");
        std::puts("");
        std::puts("Registered modules:");
        std::vector<std::string_view> moduleNames;
        for (const GameWIP::Validation::Tests::Module &module : GameWIP::Validation::Tests::registeredModules())
        {
            moduleNames.push_back(module.name);
        }
        std::ranges::sort(moduleNames);
        for (const std::string_view moduleName : moduleNames)
        {
            std::printf("  %.*s\n", static_cast<int>(moduleName.size()), moduleName.data());
        }
        std::puts("");
        std::puts("No options runs every registered module and writes the default aggregate report.");
    }
} // namespace

int main(int argc, char **argv)
{
    if (requestsHelp(GameWIP::Validation::processArguments(argc, argv)))
    {
        printHelp();
        return EXIT_SUCCESS;
    }
    const GameWIP::Validation::TestResult result = GameWIP::Validation::Tests::run(argc, argv);
    // Routed child invocations preserve the module-owned protocol exit code;
    // normal aggregate runs expose only process success or failure.
    if (result.handledChildInvocation)
    {
        return result.exitCode;
    }
    return result.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
