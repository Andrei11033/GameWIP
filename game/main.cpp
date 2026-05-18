#include "test/logger_test.h"

int main(int argc, char **argv)
{
    constexpr GameWIP::Test::LoggerTestOptions loggerTestOptions{
        .enableStressTests = false,
        .enableFatalTerminateChildTest = true,
        .enablePerformanceMetrics = false,
        .performanceIterations = 1'000'000,
        .stressThreadCount = 8,
        .stressIterationsPerThread = 20'000};

    return GameWIP::Test::runLoggerTests(argc, argv, loggerTestOptions);
}
