#pragma once

#include <cstddef>

namespace GameWIP::Test
{
    struct LoggerTestOptions
    {
        bool enableStressTests = true;
        bool enableFatalTerminateChildTest = true;
        bool enablePerformanceMetrics = true;
        std::size_t performanceIterations = 100'000;
        int stressThreadCount = 4;
        int stressIterationsPerThread = 2'000;
    };

    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options = {});
}
