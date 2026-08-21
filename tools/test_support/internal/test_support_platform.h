/// @file test_support_platform.h
/// @brief Internal platform abstraction used by the TestSupport library.

#pragma once

#include "test_support/process.h"

#include <optional>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Detail::Platform
{
    struct EnvironmentReadResult
    {
        Types::InfrastructureStatus status;
        std::optional<std::string> value;
    };

    [[nodiscard]] EnvironmentReadResult readEnvironmentVariable(std::string_view name) noexcept;
    [[nodiscard]] Types::InfrastructureStatus setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept;
    [[nodiscard]] Types::InfrastructureStatus unsetEnvironmentVariableValue(std::string_view name) noexcept;
} // namespace GameWIP::TestSupport::Detail::Platform
