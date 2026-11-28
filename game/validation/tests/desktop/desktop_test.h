/// @file desktop_test.h
/// @brief Runtime options and entry point for Desktop correctness tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Desktop library self-tests.
    struct DesktopTestOptions
    {
        // Test selection
        bool enableManualTests = false; ///< Enables visible scenarios requiring human confirmation.

        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;                                                     ///< Writes the suite report to reportPath.
        bool appendReport = true;                                                    ///< Appends instead of replacing the report.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination.
    };

    /// @brief Runs deterministic Desktop tests and optional visible manual scenarios.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime reporting and manual-test options.
    /// @return Zero when every test passes, otherwise nonzero.
    [[nodiscard]] int runDesktopTests(int argc, char **argv, const DesktopTestOptions &options = {});
} // namespace GameWIP::Test
