/// @file runner_test.h
/// @brief Validation-runner correctness tests.
///
/// This header is a source-tree validation interface for the validation-runner suite. It is not installed consumer API.

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
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt";
    };

    /// @brief Runs validation-runner parsing, selection, ordering, and propagation tests.
    /// @param options Report policy for the runner self-test suite.
    /// @return Zero when every runner test passes, nonzero otherwise.
    [[nodiscard]] int runRunnerTests(const RunnerTestOptions &options = {});
} // namespace GameWIP::Test
