#pragma once

#include <cstddef>
#include <filesystem>

namespace GameWIP::Test
{
    /// @brief Runtime toggles for the assert test suite.
    struct AssertTestOptions
    {
        /// @brief Enables the child-process assertion failure test when assertions are compiled in.
        bool enableAssertFailureChildTest = true;
        /// @brief Enables deterministic stress tests that exercise concurrent and repeated failure paths.
        bool enableStressTests = true;
        /// @brief Enables lightweight passing-path performance metrics at the end of the suite.
        bool enablePerformanceMetrics = true;
        /// @brief Enables automated interactive assert tests that use GAMEWIP_ASSERT_TEST_ACTION instead of real UI.
        bool enableInteractiveTests = true;
        /// @brief Enables manual UI tests that require the user to click real Windows dialogs.
        bool enableManualUiTests = false;
        /// @brief Number of iterations used for each passing-path performance scenario.
        std::size_t performanceIterations = 100'000;
        /// @brief Number of worker threads used by assert stress tests.
        int stressThreadCount = 4;
        /// @brief Number of repeated operations used by assert stress tests.
        int stressIterations = 1'000;
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
}
