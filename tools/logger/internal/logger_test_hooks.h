/// @file logger_test_hooks.h
/// @brief Source-tree validation hooks for deterministic Logger failures and concurrency milestones.

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

    /// @brief Arms a one-shot pause after the worker observes a false wait predicate.
    /// @warning Test-only API. The worker retains Logger's coordination mutex while paused.
    GAMEWIP_LOGGER_EXPORT void armWorkerWaitPause() noexcept;

    /// @brief Blocks until the armed worker-wait pause is reached.
    /// @warning Test-only API. Call only from an isolated child-process scenario.
    GAMEWIP_LOGGER_EXPORT void waitForWorkerWaitPause() noexcept;

    /// @brief Blocks until a queue slot is published after hook reset or arming.
    /// @warning Test-only API. Call only from an isolated child-process scenario.
    GAMEWIP_LOGGER_EXPORT void waitForQueuePublication() noexcept;

    /// @brief Releases a worker stopped at the validation-only wait pause.
    /// @warning Test-only API.
    GAMEWIP_LOGGER_EXPORT void releaseWorkerWaitPause() noexcept;

    /// @brief Arms a one-shot pause before the final active producer leaves.
    /// @warning Test-only API.
    GAMEWIP_LOGGER_EXPORT void armFinalProducerLeavePause() noexcept;

    /// @brief Blocks until the final-producer pause is reached.
    /// @warning Test-only API. Call only from an isolated child-process scenario.
    GAMEWIP_LOGGER_EXPORT void waitForFinalProducerLeavePause() noexcept;

    /// @brief Releases a producer stopped before its active count is decremented.
    /// @warning Test-only API.
    GAMEWIP_LOGGER_EXPORT void releaseFinalProducerLeavePause() noexcept;

} // namespace GameWIP::Logger::TestHooks
#endif
