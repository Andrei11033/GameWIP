/// @file test_support_platform.h
/// @brief Internal platform abstraction used by the TestSupport library.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Detail::Platform
{
    /// @brief Reads one process environment variable through the platform and CRT-compatible boundary.
    /// @param name Non-empty UTF-8 environment name without `=`.
    /// @return Current UTF-8 value, or std::nullopt when the variable is not present.
    /// @note The caller owns process-global coordination; this function performs one operation only.
    [[nodiscard]] std::optional<std::string> readEnvironmentVariable(std::string_view name);

    /// @brief Sets one process environment variable through the CRT-compatible platform boundary.
    /// @param name Non-empty UTF-8 environment name without `=`.
    /// @param value UTF-8 value. On Win32, an empty value removes the variable under `_wputenv_s` semantics.
    void setEnvironmentVariableValue(std::string_view name, std::string_view value);

    /// @brief Removes one process environment variable from the current process.
    /// @param name Non-empty UTF-8 environment name without `=`.
    void unsetEnvironmentVariableValue(std::string_view name);
} // namespace GameWIP::TestSupport::Detail::Platform
