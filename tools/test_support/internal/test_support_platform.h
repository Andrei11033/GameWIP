/// @file test_support_platform.h
/// @brief Internal platform abstraction used by the GameWIP TestSupport library.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Platform
{
    /// @brief Reads one process environment variable.
    /// @param name Environment variable name.
    /// @return Current value, or std::nullopt when the variable is not present.
    [[nodiscard]] std::optional<std::string> readEnvironmentVariable(std::string_view name);

    /// @brief Sets one process environment variable.
    /// @param name Environment variable name.
    /// @param value New environment variable value.
    void setEnvironmentVariableValue(std::string_view name, std::string_view value);

    /// @brief Removes one process environment variable from the current process.
    /// @param name Environment variable name.
    void unsetEnvironmentVariableValue(std::string_view name);
} // namespace GameWIP::TestSupport::Platform
