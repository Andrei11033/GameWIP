/// @file desktop_test.h
/// @brief Runtime options and entry point for Desktop correctness tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Desktop library self-tests.
    struct DesktopTestOptions
    {
        bool enableManualTests = false;                                         ///< Enables visible scenarios requiring human confirmation.
        bool verboseConsole = false;                                            ///< Mirrors complete suite output to stdout.
        bool writeReport = true;                                                ///< Writes progress and summaries to reportPath.
        bool appendReport = true;                                               ///< Appends instead of replacing the report.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt"; ///< Report destination.
    };

    /// @brief Runs deterministic Desktop tests and optional visible manual scenarios.
    /// @return Zero when every test passes, otherwise nonzero.
    int runDesktopTests(int argc, char **argv, const DesktopTestOptions &options = {});
} // namespace GameWIP::Test
