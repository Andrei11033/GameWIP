/// @file test_support_test.h
/// @brief Runtime options and entry point for the TestSupport self-tests.

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
        /// @brief Writes passing checks and diagnostics to stdout in addition to failures and summaries.
        bool verboseConsole = false;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it.
        bool appendReport = true;
        /// @brief Text report path used when writeReport is true.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the TestSupport library self-tests.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param options Runtime toggles for process and stress scenarios.
    /// @return Zero when every TestSupport self-test passes, nonzero otherwise.
    int runTestSupportTests(int argc, char **argv, const TestSupportTestOptions &options = {});
} // namespace GameWIP::Test
