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
        /// @brief Number of selected modules that ran.
        std::size_t modulesRun = 0;
        /// @brief Number of invoked modules that failed, or one for a runner-level failure before module execution.
        std::size_t modulesFailed = 0;
        /// @brief Normal aggregate exit code, or the exact owning-module code for a routed child invocation.
        int exitCode = 0;
        /// @brief True when the caller must return immediately because child routing handled or rejected the invocation.
        bool handledChildInvocation = false;

        /// @brief Returns true when no module or runner-level failure was recorded.
        /// @return True when modulesFailed and exitCode are both zero.
        [[nodiscard]] bool ok() const noexcept
        {
            return modulesFailed == 0 && exitCode == 0;
        }
    };

    /// @brief Outcome of one Google Benchmark invocation.
    struct BenchmarkResult
    {
        /// @brief Number returned by Google Benchmark for the selected invocation; zero is not inherently a failure.
        std::size_t benchmarksRun = 0;
        /// @brief False when Google Benchmark rejected one or more forwarded command-line arguments.
        bool argumentsValid = true;

        /// @brief Returns whether runner-level argument validation succeeded.
        /// @return True when forwarded arguments were accepted; scenario-level benchmark errors are reported by Google Benchmark output.
        [[nodiscard]] bool ok() const noexcept
        {
            return argumentsValid;
        }
    };
} // namespace GameWIP::Validation
