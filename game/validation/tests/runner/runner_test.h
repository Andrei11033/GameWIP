/// @file runner_test.h
/// @brief Validation-runner correctness tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime options for validation-runner tests.
    struct RunnerTestOptions
    {
        /// @brief Mirrors complete suite output to stdout.
        bool verboseConsole = false;
        /// @brief Writes complete test output to reportPath.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it.
        bool appendReport = true;
        /// @brief Destination for the retained test report.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs validation-runner argument and propagation tests.
    [[nodiscard]] int runRunnerTests(const RunnerTestOptions &options = {});
} // namespace GameWIP::Test
