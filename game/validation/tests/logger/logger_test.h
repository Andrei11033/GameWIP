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
        /// @brief Enables deterministic concurrency and lifecycle stress scenarios.
        bool enableStressTests = true;
        /// @brief Enables the fatal-termination subprocess scenario.
        bool enableChildCrashTests = true;
        /// @brief Enables tests that require human interaction or observation, including the real fatal-popup check.
        bool enableManualTests = false;
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Worker count used by Logger stress scenarios.
        std::size_t stressThreadCount = 4;
        /// @brief Per-worker operation count used by Logger stress scenarios.
        std::size_t stressIterationsPerThread = 2'000;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the Logger library self-tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Runtime toggles for stress, subprocess, UI, and reporting behavior.
    /// @return Zero when every Logger self-test passes, nonzero otherwise.
    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options = {});
} // namespace GameWIP::Test
