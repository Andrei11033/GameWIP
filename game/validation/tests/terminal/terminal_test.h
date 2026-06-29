/// @file terminal_test.h
/// @brief Runtime options and entry point for the Terminal self-tests.

#pragma once

#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Terminal library self-tests.
    struct TerminalTestOptions
    {
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it.
        bool appendReport = true;
        /// @brief Text report path used when writeReport is true.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the Terminal library self-tests.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param options Runtime report toggles.
    /// @return Zero when every Terminal self-test passes, nonzero otherwise.
    int runTerminalTests(int argc, char **argv, const TerminalTestOptions &options = {});
} // namespace GameWIP::Test
