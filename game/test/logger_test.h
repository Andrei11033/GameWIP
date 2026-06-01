/// @file logger_test.h
/// @brief Runtime options and entry point for the GameWIP Logger self-tests.

#pragma once

#include <cstddef>
#include <filesystem>

namespace GameWIP::Test
{
    struct LoggerTestOptions
    {
        bool enableStressTests = true;
        bool enableChildCrashTests = true;
        bool enablePerformanceMetrics = true;
        bool enableManualUiTests = false;
        bool enableLoggerPopupTest = false;
        std::size_t performanceIterations = 100'000;
        std::size_t stressThreadCount = 4;
        std::size_t stressIterationsPerThread = 2'000;
        bool writeReport = true;
        bool appendReport = true;
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options = {});
}
