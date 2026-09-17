/// @file logger_test_hooks.h
/// @brief Source-tree validation hooks for deterministic Logger failures and concurrency milestones.

#pragma once

#include "logger/internal/logger_test_export.h"
#include "logger/logger.h"

#ifndef LOGGER_INTERNAL_TEST_HOOKS
#define LOGGER_INTERNAL_TEST_HOOKS 0
#endif

#if LOGGER_INTERNAL_TEST_HOOKS
namespace GameWIP::Logger::TestHooks
{
    /// @brief Clears all pending logger test-hook failures and overrides.
    /// @warning Test-only API. Available only when LOGGER_INTERNAL_TEST_HOOKS is enabled.
    LOGGER_TEST_EXPORT void reset() noexcept;

    /// @brief Forces the next platform file-write attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_TEST_EXPORT void forceNextFileWriteFailure() noexcept;

    /// @brief Forces the next platform file-flush attempt made by the logger to fail.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_TEST_EXPORT void forceNextFileFlushFailure() noexcept;

    /// @brief Forces the next logger-owned fatal popup attempt to report a platform failure.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_TEST_EXPORT void forceNextFatalPopupFailure() noexcept;

    /// @brief Forces the next timed Logger::flush(timeout) wait to time out.
    /// @warning Test-only API. The hook is one-shot.
    LOGGER_TEST_EXPORT void forceNextTimedFlushTimeout() noexcept;

    /// @brief Arms a worker pause after dequeue and before delivery-time filtering.
    /// @warning Test-only API. Use to mutate filters while an already-accepted record is paused.
    LOGGER_TEST_EXPORT void armWorkerDeliveryPause() noexcept;

    /// @brief Waits until the worker reaches the delivery pause.
    /// @warning Test-only API. Call only from an isolated child-process scenario.
    LOGGER_TEST_EXPORT void waitForWorkerDeliveryPause() noexcept;

    /// @brief Releases the worker delivery pause.
    /// @warning Test-only API.
    LOGGER_TEST_EXPORT void releaseWorkerDeliveryPause() noexcept;

} // namespace GameWIP::Logger::TestHooks
#endif
