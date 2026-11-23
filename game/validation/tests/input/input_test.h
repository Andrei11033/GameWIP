/// @file input_test.h
/// @brief Runtime options and entry point for Input self-tests.
///
/// This header is a source-tree validation interface. It is not installed consumer API.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime reporting options for the portable Input self-tests.
    struct InputTestOptions
    {
        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/input_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs the Input library self-tests.
    /// @param options Runtime report toggles.
    /// @return Zero when every Input self-test passes, nonzero otherwise.
    [[nodiscard]] int runInputTests(const InputTestOptions &options = {});
} // namespace GameWIP::Test
