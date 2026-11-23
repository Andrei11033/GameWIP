/// @file assert_test.h
/// @brief Runtime options and entry point for the Assert self-tests.
///
/// This header is a source-tree validation interface for the Assert suite. It is not installed consumer API.

#pragma once

#include <cstddef>
#include <filesystem>

/// @brief Source-tree reusable-library self-test options and suite entry points.
namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Assert test suite.
    struct AssertTestOptions
    {
        // Test selection
        bool enableChildCrashTests = true;           ///< Enables child-process tests for crash and termination paths.
        bool enableStressTests = true;               ///< Enables concurrent and repeated failure-path tests.
        bool enableAutomatedInteractiveTests = true; ///< Uses the test-action override instead of real UI.
        bool enableManualTests = false;              ///< Enables tests that require real Windows dialogs.

        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Stress configuration
        std::size_t stressThreadCount = 4; ///< Number of worker threads used by Assert stress tests.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs the Assert library tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Test toggles for expensive or process-aborting scenarios.
    /// @return Zero when every Assert test passes, nonzero otherwise.
    [[nodiscard]] int runAssertTests(int argc, char **argv, const AssertTestOptions &options = {});
} // namespace GameWIP::Test
