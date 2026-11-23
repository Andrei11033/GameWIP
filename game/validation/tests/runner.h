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
    /// @brief Returns whether arguments request embedded startup tests or a validation child route.
    [[nodiscard]] bool requestsRun(int argc, char **argv) noexcept;

    /// @brief Shared runtime policy applied by the runner to selected correctness-test modules.
    struct RunOptions
    {
        // Test selection
        bool enableStressTests = true;                  ///< Enables deterministic stress scenarios.
        bool enableChildCrashTests = true;              ///< Enables subprocess scenarios that intentionally terminate abnormally.
        bool enableTestSupportChildProcessTests = true; ///< Enables TestSupport child-process behavior checks.
        bool enableAutomatedInteractiveTests = true;    ///< Enables non-UI simulations of interactive assertion behavior.
        bool enableManualTests = false;                 ///< Enables tests that require human interaction or observation.

        // Console output
        bool verboseConsole = false; ///< Enables full console output instead of the default minimal output.

        // Stress configuration
        std::size_t stressThreadCount = 8;                    ///< Worker count for Assert stress scenarios.
        std::size_t loggerStressIterationsPerThread = 20'000; ///< Per-worker operation count for Logger stress scenarios.

        // Report output
        bool writeReport = true;   ///< Writes validation reports to reportPath.
        bool appendReport = false; ///< Appends the first selected module instead of truncating reportPath.
        std::filesystem::path reportPath =
            "logs/validation/latest_test_report.txt"; ///< Absolute path or validated path below the executable directory.
    };

    /// @brief Routes child protocols, applies selection policy, and aggregates correctness modules.
    /// @param argc Original process argument count.
    /// @param argv Borrowed original process arguments; recognized options are not removed before module callbacks.
    /// @param options Shared policy copied into the runner; recognized command-line arguments may override the copy.
    /// @return Aggregate result, or the routed-child result when handling a child invocation.
    /// @note Single invocation only: the runner uses process-global state.
    [[nodiscard]] TestResult run(int argc, char **argv, RunOptions options = {});
} // namespace GameWIP::Validation::Tests
