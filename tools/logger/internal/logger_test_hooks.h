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
    INTERNAL_LOGGER_API void reset() noexcept;

    /// @brief Forces the next platform file-open attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    INTERNAL_LOGGER_API void forceNextFileOpenFailure() noexcept;

    /// @brief Forces the next platform file-write attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    INTERNAL_LOGGER_API void forceNextFileWriteFailure() noexcept;

    /// @brief Forces the next platform file-flush attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    INTERNAL_LOGGER_API void forceNextFileFlushFailure() noexcept;

    /// @brief Forces the next queue-entry copy to behave like an allocation failure.
    /// @warning Test-only API. The hook is one-shot.
    INTERNAL_LOGGER_API void forceNextQueueAllocationFailure() noexcept;

    /// @brief Forces the next logger-owned fatal popup attempt to report a platform failure.
    /// @warning Test-only API. The hook is one-shot.
    INTERNAL_LOGGER_API void forceNextFatalPopupFailure() noexcept;

    /// @brief Forces the next timed Logger::flush(timeout) wait to time out.
    /// @warning Test-only API. The hook is one-shot.
    INTERNAL_LOGGER_API void forceNextTimedFlushTimeout() noexcept;

    /// @brief Overrides the compiled default log directory used by empty Config::logDirectory values.
    /// @param directory Non-empty test-owned directory copied by the hook.
    /// @warning Test-only API. Set or clear the override only while Logger is stopped.
    INTERNAL_LOGGER_API void setDefaultLogDirectoryOverride(std::string_view directory);

    /// @brief Clears the test-only compiled default log-directory override.
    /// @warning Test-only API. Clear the override only while Logger is stopped.
    INTERNAL_LOGGER_API void clearDefaultLogDirectoryOverride() noexcept;
} // namespace GameWIP::Logger::TestHooks
#endif
