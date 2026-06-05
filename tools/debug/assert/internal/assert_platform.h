/// @file assert_platform.h
/// @brief Internal platform abstraction used by the GameWIP Assert library.

#pragma once

#include "debug/assert/assert.h"

#include <string_view>

namespace GameWIP::Debug::Assert::Detail::Platform
{
    /// @brief Displays a platform error popup owned by the assert library.
    /// @param title Popup title text.
    /// @param message Popup message text.
    void showErrorPopup(std::string_view title, std::string_view message) noexcept;

    /// @brief Shows an interactive fatal assertion action dialog.
    /// @param title Dialog title text.
    /// @param message Failure message text.
    /// @param defaultAction Action selected by default in the dialog.
    /// @return The action chosen by the developer, or defaultAction when no action can be chosen.
    GameWIP::Debug::Assert::FailureAction showFailureActionDialog(
        std::string_view title,
        std::string_view message,
        GameWIP::Debug::Assert::FailureAction defaultAction) noexcept;

    /// @brief Returns true when a debugger is currently attached to the process.
    bool isDebuggerAttached() noexcept;

    /// @brief Triggers the platform debugger break instruction.
    /// @details Callers decide whether a debugger must be attached first. DEBUG_BREAK() intentionally force-breaks.
    /// @note Continuing from the debugger resumes execution.
    void debugBreak() noexcept;
} // namespace GameWIP::Debug::Assert::Detail::Platform
