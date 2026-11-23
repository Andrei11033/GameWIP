/// @file platform_tests.h
/// @brief Shared helpers for platform-specific validation suites.

#pragma once

#include <string_view>

namespace GameWIP::Validation::PlatformTests
{
    /// ------------------------------------------------------------
    /// Shared native test helpers
    /// ------------------------------------------------------------

    /// @brief Debugger state relevant to native failure tests.
    enum class DebuggerStatus
    {
        Unsupported,
        Detached,
        Attached,
    };

    /// @brief Returns whether a debugger is attached or detection is unsupported.
    [[nodiscard]] DebuggerStatus debuggerStatus() noexcept;

    /// @brief Suppresses native crash and missing-file dialogs during child tests.
    void suppressCrashDialogs() noexcept;

    /// @brief Sets a process environment variable, or removes it when value is null.
    bool setEnvironmentVariable(std::string_view name, const char *value) noexcept;

} // namespace GameWIP::Validation::PlatformTests
