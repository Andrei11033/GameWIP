/// @file window_test.h
/// @brief Runtime options and entry point for Window correctness tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Window library self-tests.
    struct WindowTestOptions
    {
        bool enableManualTests = false;                                         ///< Enables visible scenarios requiring human confirmation.
        bool verboseConsole = false;                                            ///< Mirrors complete suite output to stdout.
        bool writeReport = true;                                                ///< Writes progress and summaries to reportPath.
        bool appendReport = true;                                               ///< Appends instead of replacing the report.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt"; ///< Report destination.
    };

    /// @brief Runs deterministic Window tests and optional visible manual scenarios.
    /// @return Zero when every test passes, otherwise nonzero.
    int runWindowTests(int argc, char **argv, const WindowTestOptions &options = {});
} // namespace GameWIP::Test
