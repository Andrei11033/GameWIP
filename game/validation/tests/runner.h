/// @file runner.h
/// @brief Modular correctness-test runner used by standalone and startup validation.
///
/// The runner owns command-line policy, module selection, child-process routing,
/// and aggregate reporting. Individual modules own their library-specific tests.

#pragma once

#include "validation/types.h"

#include <cstddef>
#include <filesystem>

namespace GameWIP::Validation::Tests
{
    /// @brief Shared runtime policy applied by the runner to selected correctness-test modules.
    struct RunOptions
    {
        /// @brief Enables deterministic stress scenarios.
        bool enableStressTests = true;
        /// @brief Enables subprocess scenarios that intentionally terminate abnormally.
        bool enableChildCrashTests = true;
        /// @brief Enables TestSupport child-process behavior checks.
        bool enableTestSupportChildProcessTests = true;
        /// @brief Enables non-UI simulations of interactive assertion behavior.
        bool enableAutomatedInteractiveTests = true;
        /// @brief Enables tests that require user interaction.
        bool enableManualUiTests = false;
        /// @brief Enables Logger's real fatal-popup check.
        bool enableLoggerPopupTest = false;
        /// @brief Mirrors all suite report categories to stdout instead of minimal actionable output.
        bool verboseConsole = false;
        /// @brief Worker count shared by stress scenarios.
        std::size_t stressThreadCount = 8;
        /// @brief Per-worker operation count for Logger stress scenarios.
        std::size_t loggerStressIterationsPerThread = 20'000;
        /// @brief Repetition count for Assert stress scenarios.
        std::size_t assertStressIterations = 20'000;
        /// @brief Writes the complete validation report to reportPath.
        bool writeReport = true;
        /// @brief Appends the first selected module instead of truncating reportPath.
        bool appendReport = false;
        /// @brief Absolute report path or path resolved under the GameWIP OS-temp root after lexical validation.
        std::filesystem::path reportPath = "logs/tests/latest_test_report.txt";
    };

    /// @brief Routes child protocols, applies selection policy, and aggregates correctness modules.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process arguments; recognized options are not removed before module callbacks.
    /// @param options Shared policy copied into the runner; recognized command-line arguments may override the copy.
    /// @return Aggregate result, or an exact routed-child result when handledChildInvocation is true.
    /// @note Intended for one invocation at a time because modules and reporting may coordinate process-global state.
    [[nodiscard]] TestResult run(int argc, char **argv, RunOptions options = {});
} // namespace GameWIP::Validation::Tests
