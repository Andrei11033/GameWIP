/// @file assert_platform.h
/// @brief Internal platform abstraction used by Assert runtime failure handling.
/// @details Backends implement UI presentation, debugger detection, and debugger-break behavior.

#pragma once

#include "debug/assert/assert.h"

#include <string_view>

namespace GameWIP::Debug::Assert::Detail::Platform
{
    /// @brief Displays a non-interactive Assert error popup through the active platform backend.
    /// @param title Popup title text.
    /// @param message Popup message text.
    /// @details Text is borrowed for this synchronous call. Win32 converts and bounds it,
    /// and checks popup suppression before showing UI.
    void showErrorPopup(std::string_view title, std::string_view message) noexcept;

    /// @brief Shows an interactive fatal assertion action dialog through the active platform backend.
    /// @param title Dialog title text.
    /// @param message Failure message text.
    /// @param defaultAction Action selected by default in the dialog.
    /// @details Text is borrowed until the dialog closes. The core handler owns automation/suppression
    /// policy; direct backend calls can show UI. Win32 tries Task Dialog, then a reduced MessageBox.
    /// @return The action chosen by the developer, or defaultAction when no action can be chosen.
    GameWIP::Debug::Assert::FailureAction showFailureActionDialog(
        std::string_view title,
        std::string_view message,
        GameWIP::Debug::Assert::FailureAction defaultAction) noexcept;

    /// @brief Returns true when the backend detects a debugger attached to the current process.
    /// @details Validation builds apply the active debugger override before querying the operating system.
    bool isDebuggerAttached() noexcept;

    /// @brief Triggers the platform debugger break instruction.
    /// @details Callers decide whether a debugger must be attached first. DEBUG_BREAK() intentionally force-breaks.
    /// @note Continuing from the debugger resumes execution.
    void debugBreak() noexcept;
} // namespace GameWIP::Debug::Assert::Detail::Platform
