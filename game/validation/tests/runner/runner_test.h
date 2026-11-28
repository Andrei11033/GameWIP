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
        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs validation-runner parsing, selection, ordering, and propagation tests.
    /// @param options Report policy for the runner self-test suite.
    /// @return Zero when every runner test passes, nonzero otherwise.
    [[nodiscard]] int runRunnerTests(const RunnerTestOptions &options = {});
} // namespace GameWIP::Test
