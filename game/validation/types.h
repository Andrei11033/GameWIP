/// @file types.h
/// @brief Result types shared by embedded and standalone validation runners.
///
/// These types describe process-level validation outcomes. They are used by the
/// executable integration layer and by standalone validation binaries, not by
/// reusable library consumers.

#pragma once

#include <cstddef>

/// @brief Source-tree validation results and optional executable startup facade.
namespace GameWIP::Validation
{
    /// @brief Aggregated outcome of one correctness-validation invocation.
    struct TestResult
    {
        // Aggregate outcome
        std::size_t modulesRun = 0;    ///< Number of selected modules that ran.
        std::size_t modulesFailed = 0; ///< Number of invoked modules that failed, or one for runner-level failure before module execution.
        int exitCode = 0;              ///< Normal aggregate exit code, or the owning-module code for a routed child invocation.

        // Child routing
        bool handledChildInvocation = false; ///< Whether child routing handled or rejected the invocation; return immediately when true.

        /// @brief Whether no module or runner-level failure was recorded.
        /// @return Whether modulesFailed and exitCode are both zero.
        [[nodiscard]] bool ok() const noexcept
        {
            return modulesFailed == 0 && exitCode == 0;
        }
    };

    /// @brief Outcome of one Google Benchmark invocation.
    struct BenchmarkResult
    {
        // Run status
        std::size_t benchmarksRun = 0; ///< Number returned by Google Benchmark for the selected invocation; zero is not inherently a failure.
        bool argumentsValid = true;    ///< Whether Google Benchmark accepted the forwarded command-line arguments.

        /// @brief Returns whether runner-level argument validation succeeded.
        /// @return Whether forwarded arguments were accepted; Google Benchmark reports scenario errors.
        [[nodiscard]] bool ok() const noexcept
        {
            return argumentsValid;
        }
    };
} // namespace GameWIP::Validation
