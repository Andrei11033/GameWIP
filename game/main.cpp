#include "test/assert_test.h"
#include "test/logger_test.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace
{
    struct TestRunOptions
    {
        bool runLoggerTests = true;
        bool runAssertTests = true;

        bool enableStressTests = true;
        bool enableChildCrashTests = true;
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
}

int main(int argc, char **argv)
{
    GameWIP::Test::LoggerTestOptions loggerTestOptions = makeLoggerOptions(kTestRunOptions);
    GameWIP::Test::AssertTestOptions assertTestOptions = makeAssertOptions(kTestRunOptions);

    loggerTestOptions.appendReport = kTestRunOptions.appendReport;
    assertTestOptions.appendReport = kTestRunOptions.appendReport || kTestRunOptions.runLoggerTests;

    if (hasArgument(argc, argv, "--logger-test-child=fatal-terminate"))
    {
        return GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions);
    }

    if (hasArgument(argc, argv, "--assert-test-child=assert-failure") ||
        hasArgument(argc, argv, "--assert-test-child=debug-break") ||
        hasArgument(argc, argv, "--assert-test-child=unreachable") ||
        hasArgument(argc, argv, "--assert-test-child=interactive-abort") ||
        hasArgument(argc, argv, "--assert-test-child=interactive-break"))
    {
        return GameWIP::Test::runAssertTests(argc, argv, assertTestOptions);
    }

    const int loggerResult = kTestRunOptions.runLoggerTests ? GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions) : 0;
    const int assertResult = kTestRunOptions.runAssertTests ? GameWIP::Test::runAssertTests(argc, argv, assertTestOptions) : 0;
    return loggerResult == 0 && assertResult == 0 ? 0 : 1;
}
