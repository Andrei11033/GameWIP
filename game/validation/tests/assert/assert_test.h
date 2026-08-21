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
    /// @brief Runtime toggles for the assert test suite.
    struct AssertTestOptions
    {
        /// @brief Enables child-process crash tests for assert, break, unreachable, and interactive abort/break paths.
        bool enableChildCrashTests = true;
        /// @brief Enables deterministic stress tests that exercise concurrent and repeated failure paths.
        bool enableStressTests = true;
        /// @brief Enables automated interactive assert tests that use the Assert test-action environment override instead of real UI.
        bool enableAutomatedInteractiveTests = true;
        /// @brief Enables manual tests that require the user to click real Windows dialogs.
        bool enableManualTests = false;
        /// @brief Mirrors complete suite output to stdout instead of only failures, skips, and manual instructions.
        bool verboseConsole = false;
        /// @brief Number of worker threads used by assert stress tests.
        std::size_t stressThreadCount = 4;
        /// @brief Number of repeated operations used by assert stress tests.
        std::size_t stressIterations = 1'000;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it when report writing is enabled.
        bool appendReport = true;
        /// @brief Report destination used as supplied; the shared runner normally resolves it before invocation.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the assert library tests.
    /// @param argc Borrowed process argument count for the duration of the call.
    /// @param argv Borrowed process argument values; pointed-to strings must remain valid for the call.
    /// @param options Test toggles for expensive or process-aborting scenarios.
    /// @return Zero when every assert test passes, nonzero otherwise.
    int runAssertTests(int argc, char **argv, const AssertTestOptions &options = {});
} // namespace GameWIP::Test
