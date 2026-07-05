/// @file logger_test_hooks.h
/// @brief Internal test hooks for forcing rare Logger failure paths.

#pragma once

#include "logger/logger.h"

#ifndef INTERNAL_LOGGER_TEST_HOOKS
#define INTERNAL_LOGGER_TEST_HOOKS 0
#endif

#if INTERNAL_LOGGER_TEST_HOOKS
namespace GameWIP::Logger::TestHooks
{
    /// @brief Clears all pending logger test-hook failures and overrides.
    /// @warning Test-only API. Available only when INTERNAL_LOGGER_TEST_HOOKS is enabled.
    GAMEWIP_LOGGER_EXPORT void reset() noexcept;

    /// @brief Forces the next platform file-open attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    GAMEWIP_LOGGER_EXPORT void forceNextFileOpenFailure() noexcept;

    /// @brief Forces the next platform file-write attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    GAMEWIP_LOGGER_EXPORT void forceNextFileWriteFailure() noexcept;

    /// @brief Forces the next platform file-flush attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    GAMEWIP_LOGGER_EXPORT void forceNextFileFlushFailure() noexcept;

    /// @brief Forces the next queue-entry copy to behave like an allocation failure.
    /// @warning Test-only API. The hook is one-shot.
    GAMEWIP_LOGGER_EXPORT void forceNextQueueAllocationFailure() noexcept;

    /// @brief Forces the next logger-owned fatal popup attempt to report a platform failure.
    /// @warning Test-only API. The hook is one-shot.
    GAMEWIP_LOGGER_EXPORT void forceNextFatalPopupFailure() noexcept;

    /// @brief Forces the next timed Logger::flush(timeout) wait to time out.
    /// @warning Test-only API. The hook is one-shot.
    GAMEWIP_LOGGER_EXPORT void forceNextTimedFlushTimeout() noexcept;

} // namespace GameWIP::Logger::TestHooks
#endif
