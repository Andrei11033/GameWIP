/// @file backend.h
/// @brief Internal platform abstraction used by the TestSupport library.

#pragma once

#include "test_support/process.h"

#include <optional>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Platform
{
    /// ------------------------------------------------------------
    /// Process environment contract
    /// ------------------------------------------------------------

    /// @brief Result of one platform environment read.
    using EnvironmentReadResult = GameWIP::Status::Types::Result<std::optional<std::string>>;

    /// @brief Reads one process environment variable as UTF-8 text.
    [[nodiscard]] EnvironmentReadResult readEnvironmentVariable(std::string_view name) noexcept;

    /// @brief Sets one process environment variable from UTF-8 name and value text.
    [[nodiscard]] Types::Result<void> setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept;

    /// @brief Removes one process environment variable by UTF-8 name.
    [[nodiscard]] Types::Result<void> unsetEnvironmentVariableValue(std::string_view name) noexcept;
} // namespace GameWIP::TestSupport::Platform
