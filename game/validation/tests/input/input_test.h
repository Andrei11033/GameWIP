/// @file input_test.h
/// @brief Runtime options and entry point for Input self-tests.
///
/// This header is a source-tree validation interface. It is not installed consumer API.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    struct InputTestOptions
    {
        /// @brief Mirrors complete suite output to stdout instead of only failures and summaries.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath and stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination before shared-runner resolution.
        std::filesystem::path reportPath = "logs/validation/input_test_report.txt";
    };

    /// @brief Runs the Input library self-tests.
    /// @param options Runtime report toggles.
    /// @return Zero when every Input self-test passes, nonzero otherwise.
    int runInputTests(const InputTestOptions &options = {});
} // namespace GameWIP::Test
