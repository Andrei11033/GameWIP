/// @file types.h
/// @brief Result types shared by embedded and standalone validation runners.
///
/// These types describe process-level validation outcomes. They are used by the
/// executable integration layer and by standalone validation binaries, not by
/// reusable library consumers.

#pragma once

#include <cstddef>

namespace GameWIP::Validation
{
    /// @brief Aggregated outcome of one correctness-validation invocation.
    struct TestResult
    {
        /// @brief Number of selected modules that ran.
        std::size_t modulesRun = 0;
        /// @brief Number of modules that returned a failing exit code.
        std::size_t modulesFailed = 0;
        /// @brief Process exit code requested by the validation run.
        int exitCode = 0;
        /// @brief True when a selected module handled a child-process protocol argument and the process should exit immediately.
        bool handledChildInvocation = false;

        /// @brief Returns true when no selected module failed and the requested process exit code is zero.
        [[nodiscard]] bool ok() const noexcept
        {
            return modulesFailed == 0 && exitCode == 0;
        }
    };

    /// @brief Outcome of one Google Benchmark invocation.
    struct BenchmarkResult
    {
        /// @brief Number of benchmark instances selected by Google Benchmark.
        std::size_t benchmarksRun = 0;
        /// @brief False when Google Benchmark rejected command-line arguments.
        bool argumentsValid = true;

        /// @brief Returns true when benchmark arguments were valid.
        [[nodiscard]] bool ok() const noexcept
        {
            return argumentsValid;
        }
    };
} // namespace GameWIP::Validation
