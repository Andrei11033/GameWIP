/// @file test_support_platform.h
/// @brief Internal platform abstraction used by the TestSupport library.

#pragma once

#include "test_support/test_support.h"

#include <optional>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Detail::Platform
{
    /// @brief Result of one environment-variable read.
    struct EnvironmentReadResult
    {
        /// @brief Infrastructure operation status.
        Types::InfrastructureStatus status;
        /// @brief Current value, or std::nullopt when the variable is absent.
        std::optional<std::string> value;
    };

    /// @brief Reads one process environment variable through the platform and CRT-compatible boundary.
    /// @param name Non-empty UTF-8 environment name without `=`.
    /// @return Current UTF-8 value, or std::nullopt when the variable is not present.
    /// @note The caller owns process-global coordination; this function performs one operation only.
    [[nodiscard]] EnvironmentReadResult readEnvironmentVariable(std::string_view name) noexcept;

    /// @brief Sets one process environment variable through the CRT-compatible platform boundary.
    /// @param name Non-empty UTF-8 environment name without `=`.
    /// @param value UTF-8 value. On Win32, an empty value removes the variable under `_wputenv_s` semantics.
    [[nodiscard]] Types::InfrastructureStatus setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept;

    /// @brief Removes one process environment variable from the current process.
    /// @param name Non-empty UTF-8 environment name without `=`.
    [[nodiscard]] Types::InfrastructureStatus unsetEnvironmentVariableValue(std::string_view name) noexcept;
} // namespace GameWIP::TestSupport::Detail::Platform
