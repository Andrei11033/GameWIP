/// @file base_test.h
/// @brief Source-tree entry point for internal Base mechanism tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime reporting options for internal Base mechanism tests.
    struct BaseTestOptions
    {
        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs internal Base mechanism tests.
    /// @param options Runtime report toggles.
    /// @return Zero when every Base test passes, nonzero otherwise.
    [[nodiscard]] int runBaseTests(const BaseTestOptions &options = {});
} // namespace GameWIP::Test
