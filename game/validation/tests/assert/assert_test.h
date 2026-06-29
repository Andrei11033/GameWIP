/// @file assert_test.h
/// @brief Runtime options and entry point for the Assert self-tests.

#pragma once

#include <cstddef>
#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the assert test suite.
    struct AssertTestOptions
    {
        /// @brief Enables child-process crash tests for assert, break, unreachable, and interactive abort/break paths.
        bool enableChildCrashTests = true;
        /// @brief Enables deterministic stress tests that exercise concurrent and repeated failure paths.
        bool enableStressTests = true;
        /// @brief Enables automated interactive assert tests that use INTERNAL_ASSERT_TEST_ACTION instead of real UI.
        bool enableAutomatedInteractiveTests = true;
        /// @brief Enables manual UI tests that require the user to click real Windows dialogs.
        bool enableManualUiTests = false;
        /// @brief Writes passing checks and diagnostics to stdout in addition to failures and summaries.
        bool verboseConsole = false;
        /// @brief Number of worker threads used by assert stress tests.
        std::size_t stressThreadCount = 4;
        /// @brief Number of repeated operations used by assert stress tests.
        std::size_t stressIterations = 1'000;
        /// @brief Writes test progress and summaries to reportPath in addition to stdout.
        bool writeReport = true;
        /// @brief Appends to reportPath instead of replacing it.
        bool appendReport = true;
        /// @brief Text report path used when writeReport is true.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Runs the assert library tests.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param options Test toggles for expensive or process-aborting scenarios.
    /// @return Zero when every assert test passes, nonzero otherwise.
    int runAssertTests(int argc, char **argv, const AssertTestOptions &options = {});
} // namespace GameWIP::Test
