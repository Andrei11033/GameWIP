#pragma once

#include "debug/assert/assert.h"

#include <string_view>

#ifndef GAMEWIP_ASSERT_TEST_HOOKS
#define GAMEWIP_ASSERT_TEST_HOOKS 0
#endif

#if GAMEWIP_ASSERT_TEST_HOOKS
namespace GameWIP::Debug::Assert::TestHooks
{
    /// @brief Clears all pending assert test-hook failures and overrides.
    /// @warning Test-only API. Available only when GAMEWIP_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_API void reset() noexcept;

    /// @brief Forces the next Win32 TaskDialogIndirect action dialog attempt to fail.
    /// @warning Test-only API. The hook is one-shot and is intended to exercise fallback behavior.
    GAMEWIP_ASSERT_API void forceNextTaskDialogFailure() noexcept;

    /// @brief Forces the next Win32 MessageBox fallback attempt to fail.
    /// @warning Test-only API. The hook is one-shot and is intended to exercise default-action fallback behavior.
    GAMEWIP_ASSERT_API void forceNextMessageBoxFailure() noexcept;

    /// @brief Overrides the debugger-attached query used by assert failure handling.
    /// @param attached Value returned while the override is active.
    /// @warning Test-only API. Persistent until clearDebuggerAttachedOverride() or reset().
    GAMEWIP_ASSERT_API void setDebuggerAttachedOverride(bool attached) noexcept;

    /// @brief Clears the debugger-attached override.
    /// @warning Test-only API.
    GAMEWIP_ASSERT_API void clearDebuggerAttachedOverride() noexcept;

    /// @brief Overrides popup-suppression checks used by assert failure handling.
    /// @param suppressed Value returned while the override is active.
    /// @warning Test-only API. Persistent until clearPopupSuppressedOverride() or reset().
    GAMEWIP_ASSERT_API void setPopupSuppressedOverride(bool suppressed) noexcept;

    /// @brief Clears the popup-suppression override.
    /// @warning Test-only API.
    GAMEWIP_ASSERT_API void clearPopupSuppressedOverride() noexcept;

    /// @brief Queries the platform debugger-attached state through the assert backend.
    /// @return True when the backend reports a debugger as attached, including any active test override.
    /// @warning Test-only API. Available only when GAMEWIP_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_API bool debuggerAttachedForTest() noexcept;

    /// @brief Exercises the platform interactive failure dialog path through the assert backend.
    /// @param title Dialog title text.
    /// @param message Dialog body text.
    /// @param defaultAction Action returned when the backend cannot show a dialog.
    /// @return Selected or fallback action.
    /// @warning Test-only API. Available only when GAMEWIP_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_API FailureAction showFailureActionDialogForTest(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept;

    /// @brief Exercises the platform error-popup path through the assert backend.
    /// @param title Popup title text.
    /// @param message Popup body text.
    /// @warning Test-only API. Available only when GAMEWIP_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_API void showErrorPopupForTest(std::string_view title, std::string_view message) noexcept;

    namespace Detail
    {
        /// @brief Consumes the one-shot TaskDialog failure hook.
        GAMEWIP_ASSERT_API bool consumeNextTaskDialogFailure() noexcept;
        /// @brief Consumes the one-shot MessageBox failure hook.
        GAMEWIP_ASSERT_API bool consumeNextMessageBoxFailure() noexcept;
        /// @brief Reads the debugger-attached override when one is active.
        GAMEWIP_ASSERT_API bool debuggerAttachedOverride(bool &attached) noexcept;
        /// @brief Reads the popup-suppressed override when one is active.
        GAMEWIP_ASSERT_API bool popupSuppressedOverride(bool &suppressed) noexcept;
    }
}
#endif
