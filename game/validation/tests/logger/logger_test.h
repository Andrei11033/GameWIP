/// @file logger_test.h
/// @brief Runtime options and entry point for the Logger self-tests.
///
/// This header is a source-tree validation interface for the Logger suite. It is not installed consumer API.

#pragma once

#include <cstddef>
#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the Logger self-tests.
    struct LoggerTestOptions
    {
        // Test selection
        bool enableStressTests = true;     ///< Enables concurrency and lifecycle stress scenarios.
        bool enableChildCrashTests = true; ///< Enables fatal-termination and paused-worker subprocess scenarios.
        bool enableManualTests = false;    ///< Enables tests requiring human interaction or observation.

        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Stress configuration
        std::size_t stressIterationsPerThread = 2'000; ///< Per-worker operation count for Logger stress scenarios.

        // Report output
        bool writeReport = true;  ///< Writes the suite report to reportPath.
        bool appendReport = true; ///< Appends to reportPath instead of replacing it when report writing is enabled.
        std::filesystem::path reportPath = "logs/validation/latest_test_report.txt"; ///< Report destination before shared-runner resolution.
    };

    /// @brief Runs the Logger library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime toggles for stress, subprocess, UI, and reporting behavior.
    /// @return Zero when every Logger self-test passes, nonzero otherwise.
    [[nodiscard]] int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options = {});
} // namespace GameWIP::Test
