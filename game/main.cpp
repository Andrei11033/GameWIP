#include "test/assert_test.h"
#include "test/logger_test.h"

#include <filesystem>
#include <string_view>

namespace
{
    struct TestRunOptions
    {
        bool runLoggerTests = true;
        bool runAssertTests = true;

        bool enableStressTests = true;
        bool enableFatalChildTests = true;
        bool enablePerformanceMetrics = true;
        bool enableInteractiveTests = true;
        bool enableManualUiTests = true;
        bool enableLoggerPopupTest = true;

        std::size_t performanceIterations = 1'000'000;
        int stressThreadCount = 8;
        int loggerStressIterationsPerThread = 20'000;
        int assertStressIterations = 20'000;

        bool writeReport = true;
        bool appendReport = false;
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    const TestRunOptions testRunOptions{};

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
            .enableFatalTerminateChildTest = options.enableFatalChildTests,
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
            .enableAssertFailureChildTest = options.enableFatalChildTests,
            .enableStressTests = options.enableStressTests,
            .enablePerformanceMetrics = options.enablePerformanceMetrics,
            .enableInteractiveTests = options.enableInteractiveTests,
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
    GameWIP::Test::LoggerTestOptions loggerTestOptions = makeLoggerOptions(testRunOptions);
    GameWIP::Test::AssertTestOptions assertTestOptions = makeAssertOptions(testRunOptions);

    loggerTestOptions.appendReport = testRunOptions.appendReport;
    assertTestOptions.appendReport = testRunOptions.appendReport || testRunOptions.runLoggerTests;

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

    const int loggerResult = testRunOptions.runLoggerTests ? GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions) : 0;
    const int assertResult = testRunOptions.runAssertTests ? GameWIP::Test::runAssertTests(argc, argv, assertTestOptions) : 0;
    return loggerResult == 0 && assertResult == 0 ? 0 : 1;
}
