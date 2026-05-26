#include "test/assert_test.h"
#include "test/logger_test.h"

#include <string_view>

namespace
{
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
}

int main(int argc, char **argv)
{
    constexpr bool runLoggerTests = false;
    constexpr bool runAssertTests = true;

    constexpr GameWIP::Test::LoggerTestOptions loggerTestOptions{
        .enableStressTests = true,
        .enableFatalTerminateChildTest = true,
        .enablePerformanceMetrics = true,
        .performanceIterations = 1'000'000,
        .stressThreadCount = 8,
        .stressIterationsPerThread = 20'000};

    constexpr GameWIP::Test::AssertTestOptions assertTestOptions{
        .enableAssertFailureChildTest = true,
        .enablePerformanceMetrics = true,
        .performanceIterations = 1'000'000};

    if (hasArgument(argc, argv, "--logger-test-child=fatal-terminate"))
    {
        return GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions);
    }

    if (hasArgument(argc, argv, "--assert-test-child=assert-failure") ||
        hasArgument(argc, argv, "--assert-test-child=debug-break") ||
        hasArgument(argc, argv, "--assert-test-child=unreachable"))
    {
        return GameWIP::Test::runAssertTests(argc, argv, assertTestOptions);
    }

    const int loggerResult = runLoggerTests ? GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions) : 0;
    const int assertResult = runAssertTests ? GameWIP::Test::runAssertTests(argc, argv, assertTestOptions) : 0;
    return loggerResult == 0 && assertResult == 0 ? 0 : 1;
}
