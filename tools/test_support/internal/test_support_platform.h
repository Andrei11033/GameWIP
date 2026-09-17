/// @file test_support_platform.h
/// @brief Internal platform abstraction used by the TestSupport library.

#pragma once

#include "test_support/process.h"

#include <optional>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Detail::Platform
{
    /// @brief Result of one platform environment read.
    struct EnvironmentReadResult
    {
        Types::InfrastructureStatus status; ///< Read status; success with no value means the variable is absent.
        std::optional<std::string> value;   ///< UTF-8 variable value when present.
    };

    /// @brief Reads one process environment variable as UTF-8 text.
    [[nodiscard]] EnvironmentReadResult readEnvironmentVariable(std::string_view name) noexcept;

    /// @brief Sets one process environment variable from UTF-8 name and value text.
    [[nodiscard]] Types::InfrastructureStatus setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept;

    /// @brief Removes one process environment variable by UTF-8 name.
    [[nodiscard]] Types::InfrastructureStatus unsetEnvironmentVariableValue(std::string_view name) noexcept;
} // namespace GameWIP::TestSupport::Detail::Platform
