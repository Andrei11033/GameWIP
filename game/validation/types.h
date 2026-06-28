/// @file types.h
/// @brief Result types shared by embedded and standalone validation runners.

#pragma once

#include <cstddef>

namespace GameWIP::Validation
{
    struct TestResult
    {
        std::size_t modulesRun = 0;
        std::size_t modulesFailed = 0;
        int exitCode = 0;
        bool handledChildInvocation = false;

        [[nodiscard]] bool ok() const noexcept
        {
            return modulesFailed == 0 && exitCode == 0;
        }
    };

    struct BenchmarkResult
    {
        std::size_t benchmarksRun = 0;
        bool argumentsValid = true;

        [[nodiscard]] bool ok() const noexcept
        {
            return argumentsValid;
        }
    };
} // namespace GameWIP::Validation
