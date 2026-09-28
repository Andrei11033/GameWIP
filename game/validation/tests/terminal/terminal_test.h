/// @file terminal_test.h
/// @brief Runtime options and entry point for the Terminal self-tests.
///
/// This header is a source-tree validation interface for the Terminal suite. It is not installed consumer API.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Terminal library self-tests.
    struct TerminalTestOptions
    {
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Enables tests requiring manual interaction; otherwise they are skipped.
        bool enableManualTests = false;
        /// @brief Writes test progress and summaries to reportPath and stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination before shared-runner resolution.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt";
    };

    /// @brief Runs the Terminal library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime report toggles.
    /// @return Zero when every Terminal self-test passes, nonzero otherwise.
    int runTerminalTests(int argc, char **argv, const TerminalTestOptions &options = {});
} // namespace GameWIP::Test
