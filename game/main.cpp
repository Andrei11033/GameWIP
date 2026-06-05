/// @file main.cpp
/// @brief GameWIP executable entry point and runtime test-suite dispatcher.

#include "test/assert_test.h"
#include "test/io_test.h"
#include "test/logger_test.h"
#include "test/terminal_test.h"
#include "test/test_support_test.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace
{
    struct TestRunOptions
    {
        bool runIOTests = true;
        bool runTerminalTests = true;
        bool runTestSupportTests = true;
        bool runLoggerTests = true;
        bool runAssertTests = true;

        bool enableStressTests = true;
        bool enableChildCrashTests = true;
        bool enableTestSupportChildProcessTests = true;
        bool enablePerformanceMetrics = true;
        bool enableAutomatedInteractiveTests = true;
        bool enableManualUiTests = false;
        bool enableLoggerPopupTest = false;

        std::size_t performanceIterations = 1'000'000;
        std::size_t stressThreadCount = 8;
        std::size_t loggerStressIterationsPerThread = 20'000;
        std::size_t assertStressIterations = 20'000;

        bool writeReport = true;
        bool appendReport = false;
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    const TestRunOptions kTestRunOptions{};

    bool hasArgument(int argc, char **argv, const char *argument)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]) == argument)
            {
                return true;
            }
        }
        return false;
    }

    bool hasArgumentPrefix(int argc, char **argv, std::string_view prefix)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]).starts_with(prefix))
            {
                return true;
            }
        }
        return false;
    }

    TestRunOptions makeRunOptions(int argc, char **argv)
    {
        TestRunOptions options = kTestRunOptions;

        if (hasArgument(argc, argv, "--no-manual-ui"))
        {
            options.enableManualUiTests = false;
            options.enableLoggerPopupTest = false;
        }

        if (hasArgument(argc, argv, "--no-logger-popup"))
        {
            options.enableLoggerPopupTest = false;
        }

        if (hasArgument(argc, argv, "--io-only"))
        {
            options.runTerminalTests = false;
            options.runTestSupportTests = false;
            options.runLoggerTests = false;
            options.runAssertTests = false;
            options.enableManualUiTests = false;
            options.enableLoggerPopupTest = false;
        }

        if (hasArgument(argc, argv, "--terminal-only"))
        {
            options.runIOTests = false;
            options.runTestSupportTests = false;
            options.runLoggerTests = false;
            options.runAssertTests = false;
            options.enableManualUiTests = false;
            options.enableLoggerPopupTest = false;
        }

        if (hasArgument(argc, argv, "--test-support-only"))
        {
            options.runIOTests = false;
            options.runTerminalTests = false;
            options.runLoggerTests = false;
            options.runAssertTests = false;
            options.enableManualUiTests = false;
            options.enableLoggerPopupTest = false;
        }

        if (hasArgument(argc, argv, "--test-support-manual"))
        {
            options.runIOTests = false;
            options.runTerminalTests = false;
            options.runLoggerTests = false;
            options.runAssertTests = false;
            options.runTestSupportTests = true;
            options.enableManualUiTests = true;
            options.enableLoggerPopupTest = false;
        }

        if (hasArgument(argc, argv, "--no-test-support-child-process"))
        {
            options.enableTestSupportChildProcessTests = false;
        }

        if (hasArgument(argc, argv, "--no-io-tests"))
        {
            options.runIOTests = false;
        }

        if (hasArgument(argc, argv, "--no-terminal-tests"))
        {
            options.runTerminalTests = false;
        }

        return options;
    }

    GameWIP::Test::IOTestOptions makeIOOptions(const TestRunOptions &options)
    {
        return GameWIP::Test::IOTestOptions{
            .writeReport = options.writeReport,
            .appendReport = options.appendReport,
            .reportPath = options.reportPath};
    }

    GameWIP::Test::TerminalTestOptions makeTerminalOptions(const TestRunOptions &options)
    {
        return GameWIP::Test::TerminalTestOptions{
            .writeReport = options.writeReport,
            .appendReport = options.appendReport,
            .reportPath = options.reportPath};
    }

    GameWIP::Test::TestSupportTestOptions makeTestSupportOptions(const TestRunOptions &options)
    {
        return GameWIP::Test::TestSupportTestOptions{
            .enableChildProcessTests = options.enableTestSupportChildProcessTests,
            .enableStressTests = options.enableStressTests,
            .enableManualTests = options.enableManualUiTests,
            .writeReport = options.writeReport,
            .appendReport = options.appendReport,
            .reportPath = options.reportPath};
    }

    GameWIP::Test::LoggerTestOptions makeLoggerOptions(const TestRunOptions &options)
    {
        return GameWIP::Test::LoggerTestOptions{
            .enableStressTests = options.enableStressTests,
            .enableChildCrashTests = options.enableChildCrashTests,
            .enablePerformanceMetrics = options.enablePerformanceMetrics,
            .enableManualUiTests = options.enableManualUiTests,
            .enableLoggerPopupTest = options.enableLoggerPopupTest,
            .performanceIterations = options.performanceIterations,
            .stressThreadCount = options.stressThreadCount,
            .stressIterationsPerThread = options.loggerStressIterationsPerThread,
            .writeReport = options.writeReport,
            .appendReport = options.appendReport,
            .reportPath = options.reportPath};
    }

    GameWIP::Test::AssertTestOptions makeAssertOptions(const TestRunOptions &options)
    {
        return GameWIP::Test::AssertTestOptions{
            .enableChildCrashTests = options.enableChildCrashTests,
            .enableStressTests = options.enableStressTests,
            .enablePerformanceMetrics = options.enablePerformanceMetrics,
            .enableAutomatedInteractiveTests = options.enableAutomatedInteractiveTests,
            .enableManualUiTests = options.enableManualUiTests,
            .performanceIterations = options.performanceIterations,
            .stressThreadCount = options.stressThreadCount,
            .stressIterations = options.assertStressIterations,
            .writeReport = options.writeReport,
            .appendReport = options.appendReport,
            .reportPath = options.reportPath};
    }
} // namespace

int main(int argc, char **argv)
{
    const TestRunOptions runOptions = makeRunOptions(argc, argv);
    GameWIP::Test::IOTestOptions ioTestOptions = makeIOOptions(runOptions);
    GameWIP::Test::TerminalTestOptions terminalTestOptions = makeTerminalOptions(runOptions);
    GameWIP::Test::TestSupportTestOptions testSupportTestOptions = makeTestSupportOptions(runOptions);
    GameWIP::Test::LoggerTestOptions loggerTestOptions = makeLoggerOptions(runOptions);
    GameWIP::Test::AssertTestOptions assertTestOptions = makeAssertOptions(runOptions);

    ioTestOptions.appendReport = runOptions.appendReport;
    terminalTestOptions.appendReport = runOptions.appendReport || runOptions.runIOTests;
    testSupportTestOptions.appendReport = runOptions.appendReport || runOptions.runIOTests || runOptions.runTerminalTests;
    loggerTestOptions.appendReport =
        runOptions.appendReport || runOptions.runIOTests || runOptions.runTerminalTests || runOptions.runTestSupportTests;
    assertTestOptions.appendReport = runOptions.appendReport || runOptions.runIOTests || runOptions.runTerminalTests ||
                                     runOptions.runTestSupportTests || runOptions.runLoggerTests;

    if (hasArgumentPrefix(argc, argv, "--test-support-test-child="))
    {
        return GameWIP::Test::runTestSupportTests(argc, argv, testSupportTestOptions);
    }

    if (hasArgument(argc, argv, "--logger-test-child=fatal-terminate"))
    {
        return GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions);
    }

    if (hasArgument(argc, argv, "--assert-test-child=assert-failure") || hasArgument(argc, argv, "--assert-test-child=debug-break") ||
        hasArgument(argc, argv, "--assert-test-child=unreachable") || hasArgument(argc, argv, "--assert-test-child=interactive-abort") ||
        hasArgument(argc, argv, "--assert-test-child=interactive-break"))
    {
        return GameWIP::Test::runAssertTests(argc, argv, assertTestOptions);
    }

    const int ioResult = runOptions.runIOTests ? GameWIP::Test::runIOTests(argc, argv, ioTestOptions) : 0;
    const int terminalResult = runOptions.runTerminalTests ? GameWIP::Test::runTerminalTests(argc, argv, terminalTestOptions) : 0;
    const int testSupportResult = runOptions.runTestSupportTests ? GameWIP::Test::runTestSupportTests(argc, argv, testSupportTestOptions) : 0;
    const int loggerResult = runOptions.runLoggerTests ? GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions) : 0;
    const int assertResult = runOptions.runAssertTests ? GameWIP::Test::runAssertTests(argc, argv, assertTestOptions) : 0;

    return ioResult == 0 && terminalResult == 0 && testSupportResult == 0 && loggerResult == 0 && assertResult == 0 ? 0 : 1;
}
