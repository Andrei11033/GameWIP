/// @file window_test.h
/// @brief Runtime options and entry point for Window correctness tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Window library self-tests.
    struct WindowTestOptions
    {
        bool verboseConsole = false;                                            ///< Mirrors complete suite output to stdout.
        bool writeReport = true;                                                ///< Writes progress and summaries to reportPath.
        bool appendReport = true;                                               ///< Appends instead of replacing the report.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt"; ///< Report destination.
    };

    /// @brief Runs deterministic portable and hidden-native Window tests.
    /// @return Zero when every test passes, otherwise nonzero.
    int runWindowTests(int argc, char **argv, const WindowTestOptions &options = {});
} // namespace GameWIP::Test
