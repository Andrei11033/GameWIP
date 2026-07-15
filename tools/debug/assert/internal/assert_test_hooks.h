/// @file assert_test_hooks.h
/// @brief Source-tree-only test hooks for deterministic Assert validation.
/// @details This header is excluded from installed public file sets. Hooks are enabled only when
/// `INTERNAL_ASSERT_TEST_HOOKS` is `1`.

#pragma once

#include "debug/assert/assert.h"

#include <string_view>

#ifndef INTERNAL_ASSERT_TEST_HOOKS
#define INTERNAL_ASSERT_TEST_HOOKS 0
#endif

#if INTERNAL_ASSERT_TEST_HOOKS
namespace GameWIP::Debug::Assert::TestHooks
{
    /// @brief Clears all pending one-shot failures and persistent overrides.
    /// @warning Test-only API. Available only when INTERNAL_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_EXPORT void reset() noexcept;

    /// @brief Forces the next primary platform action-dialog attempt to use the fallback path.
    /// @warning Test-only API. The hook is one-shot and is intended to exercise fallback behavior.
    GAMEWIP_ASSERT_EXPORT void forceNextActionDialogFailure() noexcept;

    /// @brief Forces the next fallback action-dialog attempt to return the default action.
    /// @warning Test-only API. The hook is one-shot and is intended to exercise default-action fallback behavior.
    GAMEWIP_ASSERT_EXPORT void forceNextFallbackActionDialogFailure() noexcept;

    /// @brief Overrides the debugger-attached query used by assert failure handling.
    /// @param attached Value returned while the override is active.
    /// @warning Test-only API. Persistent until clearDebuggerAttachedOverride() or reset().
    GAMEWIP_ASSERT_EXPORT void setDebuggerAttachedOverride(bool attached) noexcept;

    /// @brief Clears the debugger-attached override.
    /// @warning Test-only API.
    GAMEWIP_ASSERT_EXPORT void clearDebuggerAttachedOverride() noexcept;

    /// @brief Overrides popup-suppression checks used by Assert failure handling.
    /// @param suppressed Value returned while the override is active.
    /// @warning Test-only API. Persistent until clearPopupSuppressedOverride() or reset().
    GAMEWIP_ASSERT_EXPORT void setPopupSuppressedOverride(bool suppressed) noexcept;

    /// @brief Clears the popup-suppression override.
    /// @warning Test-only API.
    GAMEWIP_ASSERT_EXPORT void clearPopupSuppressedOverride() noexcept;

    /// @brief Queries the platform debugger-attached state through the assert backend.
    /// @return True when the backend reports a debugger as attached, including any active test override.
    /// @warning Test-only API. Available only when INTERNAL_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_EXPORT bool debuggerAttachedForTest() noexcept;

    /// @brief Exercises the platform interactive failure dialog path through the assert backend.
    /// @param title Dialog title text.
    /// @param message Dialog body text.
    /// @param defaultAction Action returned when the backend cannot show a dialog.
    /// @return Selected or fallback action.
    /// @warning Test-only API. Available only when INTERNAL_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_EXPORT FailureAction
    showFailureActionDialogForTest(std::string_view title, std::string_view message, FailureAction defaultAction) noexcept;

    /// @brief Exercises the platform error-popup path through the assert backend.
    /// @param title Popup title text.
    /// @param message Popup body text.
    /// @warning Test-only API. Available only when INTERNAL_ASSERT_TEST_HOOKS is enabled.
    GAMEWIP_ASSERT_EXPORT void showErrorPopupForTest(std::string_view title, std::string_view message) noexcept;

} // namespace GameWIP::Debug::Assert::TestHooks

namespace GameWIP::Debug::Assert::Detail::TestHooks
{
    /// @brief Consumes the one-shot primary action-dialog failure hook.
    GAMEWIP_ASSERT_EXPORT bool consumeNextActionDialogFailure() noexcept;
    /// @brief Consumes the one-shot fallback action-dialog failure hook.
    GAMEWIP_ASSERT_EXPORT bool consumeNextFallbackActionDialogFailure() noexcept;
    /// @brief Reads the debugger-attached override when one is active.
    GAMEWIP_ASSERT_EXPORT bool debuggerAttachedOverride(bool &attached) noexcept;
    /// @brief Reads the popup-suppressed override when one is active.
    GAMEWIP_ASSERT_EXPORT bool popupSuppressedOverride(bool &suppressed) noexcept;
} // namespace GameWIP::Debug::Assert::Detail::TestHooks
#endif
