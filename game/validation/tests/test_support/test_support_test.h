/// @file test_support_test.h
/// @brief Runtime options and entry point for the TestSupport self-tests.
///
/// This header is a source-tree validation interface for the TestSupport suite. It is not installed consumer API.

#pragma once

#include <cstddef>
#include <filesystem>

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Private child-mode routing
    /// ------------------------------------------------------------

    namespace Internal
    {
        /// @brief Checks whether arguments select a private TestSupport child mode.
        [[nodiscard]] bool hasTestSupportChildArguments(int argc, char **argv);
    } // namespace Internal

    /// @brief Runtime toggles for the TestSupport library self-tests.
    struct TestSupportTestOptions
    {
        // Test selection
        bool enableChildProcessTests = true; ///< Enables process launch, output, environment, and timeout tests.
        bool enableStressTests = true;       ///< Enables lightweight stress-helper tests.
        bool enableManualTests = false;      ///< Enables prompt checks that require user input.

        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs the TestSupport library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime toggles for process and stress scenarios.
    /// @return Zero when every TestSupport self-test passes, nonzero otherwise.
    [[nodiscard]] int runTestSupportTests(int argc, char **argv, const TestSupportTestOptions &options = {});
} // namespace GameWIP::Test
