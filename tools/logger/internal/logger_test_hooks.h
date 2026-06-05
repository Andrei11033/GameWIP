/// @file logger_test_hooks.h
/// @brief Internal test hooks for forcing rare GameWIP Logger failure paths.

#pragma once

#include "logger/logger.h"

#ifndef GAMEWIP_LOGGER_TEST_HOOKS
#define GAMEWIP_LOGGER_TEST_HOOKS 0
#endif

#if GAMEWIP_LOGGER_TEST_HOOKS
namespace GameWIP::Logger::TestHooks
{
    /// @brief Clears all pending logger test-hook failures and overrides.
    /// @warning Test-only API. Available only when GAMEWIP_LOGGER_TEST_HOOKS is enabled.
    LOGGER_API void reset() noexcept;

    /// @brief Forces the next platform file-open attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_API void forceNextFileOpenFailure() noexcept;

    /// @brief Forces the next platform file-write attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_API void forceNextFileWriteFailure() noexcept;

    /// @brief Forces the next platform file-flush attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_API void forceNextFileFlushFailure() noexcept;

    /// @brief Forces the next queue-entry copy to behave like an allocation failure.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_API void forceNextQueueAllocationFailure() noexcept;

    /// @brief Forces the next logger-owned fatal popup attempt to report a platform failure.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_API void forceNextFatalPopupFailure() noexcept;

    /// @brief Forces the next timed Logger::flush(timeout) wait to time out.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_API void forceNextTimedFlushTimeout() noexcept;
}
#endif
