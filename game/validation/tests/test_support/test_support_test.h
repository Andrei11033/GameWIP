/// @file test_support_test.h
/// @brief Runtime options and entry point for the TestSupport self-tests.
///
/// This header is a source-tree validation interface for the TestSupport suite. It is not installed consumer API.

#pragma once

#include <cstddef>
#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the TestSupport library self-tests.
    struct TestSupportTestOptions
    {
        /// @brief Enables child-process launch, output capture, environment, and timeout tests.
        bool enableChildProcessTests = true;
        /// @brief Enables lightweight stress-helper tests.
        bool enableStressTests = true;
        /// @brief Enables manual prompt checks that require user input.
        bool enableManualTests = false;
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the TestSupport library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime toggles for process and stress scenarios.
    /// @return Zero when every TestSupport self-test passes, nonzero otherwise.
    int runTestSupportTests(int argc, char **argv, const TestSupportTestOptions &options = {});
} // namespace GameWIP::Test
