/// @file unicode_test.h
/// @brief Runtime options and entry point for the Unicode self-tests.
///
/// This header is a source-tree validation interface for the Unicode suite. It is not installed consumer API.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Unicode library self-tests.
    struct UnicodeTestOptions
    {
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt";
    };

    /// @brief Runs the Unicode library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime report toggles.
    /// @return Zero when every Unicode self-test passes, nonzero otherwise.
    int runUnicodeTests(int argc, char **argv, const UnicodeTestOptions &options = {});
} // namespace GameWIP::Test
