/// @file module.cpp
/// @brief Registers the Logger correctness-test module with the validation runner.

#include "validation/tests/logger/logger_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
    /// @brief Detects fatal-termination child invocations owned by Logger tests.
    bool handlesChildArguments(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] == nullptr)
            {
                continue;
            }

            const std::string_view argument(argv[index]);
            if (argument == "--logger-test-child=fatal-terminate"
#if INTERNAL_LOGGER_TEST_HOOKS
                || argument == "--logger-test-child=enqueue-wakeup" || argument == "--logger-test-child=shutdown-wakeup"
#endif
            )
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Maps shared runner policy to Logger-specific options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::LoggerTestOptions options;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableChildCrashTests = invocation.options.enableChildCrashTests;
        options.enableManualUiTests = invocation.options.enableManualUiTests;
        options.enableLoggerPopupTest = invocation.options.enableLoggerPopupTest;
        options.verboseConsole = invocation.options.verboseConsole;
        options.stressThreadCount = invocation.options.stressThreadCount;
        options.stressIterationsPerThread = invocation.options.loggerStressIterationsPerThread;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runLoggerTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic Logger module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "logger",
        .order = 50,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
