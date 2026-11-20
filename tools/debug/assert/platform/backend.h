/// @file backend.h
/// @brief Private platform backend contract for Assert failure handling.
/// @details Backends implement UI presentation, debugger detection, and debugger-break behavior.

#pragma once

#include "debug/assert/assert.h"

#include <string_view>

namespace GameWIP::Debug::Assert::Detail::Platform
{
    /// ------------------------------------------------------------
    /// Failure presentation and debugger operations
    /// ------------------------------------------------------------

    /// @brief Displays a non-interactive Assert error popup through the active platform backend.
    /// @param title Popup title text.
    /// @param message Popup message text.
    /// @details Text is borrowed for this synchronous call. The backend handles native text
    /// conversion and presentation limits.
    void showErrorPopup(std::string_view title, std::string_view message) noexcept;

    /// @brief Shows an interactive fatal assertion action dialog through the active platform backend.
    /// @param title Dialog title text.
    /// @param message Failure message text.
    /// @param defaultAction Action selected by default in the dialog.
    /// @details Text is borrowed until the dialog closes. The core selects the default action and
    /// owns automation policy; the backend maps native dialog results to FailureAction.
    /// @return The action chosen by the developer, or defaultAction when no action can be chosen.
    GameWIP::Debug::Assert::FailureAction showFailureActionDialog(
        std::string_view title,
        std::string_view message,
        GameWIP::Debug::Assert::FailureAction defaultAction) noexcept;

    /// @brief Returns true when the backend detects a debugger attached to the current process.
    bool isDebuggerAttached() noexcept;

    /// @brief Triggers the platform debugger break instruction.
    /// @details Callers decide whether a debugger must be attached first. DEBUG_BREAK() intentionally force-breaks.
    /// @note Continuing from the debugger resumes execution.
    void debugBreak() noexcept;
} // namespace GameWIP::Debug::Assert::Detail::Platform
