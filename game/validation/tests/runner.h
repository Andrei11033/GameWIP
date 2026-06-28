/// @file runner.h
/// @brief Modular correctness-test runner.

#pragma once

#include "validation/types.h"

#include <cstddef>
#include <filesystem>

namespace GameWIP::Validation::Tests
{
    struct RunOptions
    {
        bool enableStressTests = true;
        bool enableChildCrashTests = true;
        bool enableTestSupportChildProcessTests = true;
        bool enableAutomatedInteractiveTests = true;
        bool enableManualUiTests = false;
        bool enableLoggerPopupTest = false;
        std::size_t stressThreadCount = 8;
        std::size_t loggerStressIterationsPerThread = 20'000;
        std::size_t assertStressIterations = 20'000;
        bool writeReport = true;
        bool appendReport = false;
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    [[nodiscard]] TestResult run(int argc, char **argv, RunOptions options = {});
} // namespace GameWIP::Validation::Tests
