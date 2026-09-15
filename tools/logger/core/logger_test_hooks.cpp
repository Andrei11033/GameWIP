/// @file logger_test_hooks.cpp
/// @brief Logger internal test hook definitions.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
#if LOGGER_INTERNAL_TEST_HOOKS
    LoggerTestHookState loggerTestHookState;

    namespace
    {
        /// @brief Publishes one coordination milestone and wakes atomic waiters.
        void publishHookMilestone(std::atomic_bool &milestone) noexcept
        {
            milestone.store(true, std::memory_order_release);
            milestone.notify_all();
        }

        /// @brief Blocks until one coordination milestone has been published.
        void waitForHookMilestone(const std::atomic_bool &milestone) noexcept
        {
            milestone.wait(false, std::memory_order_acquire);
        }
    } // namespace

    // ------------------------------------------------------------
    // Hook consumption used by Logger internals
    // ------------------------------------------------------------

    bool consumeTestHook(std::atomic_bool &flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }

    void resetLoggerTestHooks() noexcept
    {
        loggerTestHookState.nextFileWriteFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileFlushFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFatalPopupFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextTimedFlushTimeout.store(false, std::memory_order_release);

        loggerTestHookState.pauseBeforeWorkerDelivery.store(false, std::memory_order_release);
        loggerTestHookState.workerDeliveryReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseWorkerDelivery.store(false, std::memory_order_release);
    }

    void pauseWorkerBeforeDeliveryForTest() noexcept
    {
        if (!consumeTestHook(loggerTestHookState.pauseBeforeWorkerDelivery))
        {
            return;
        }

        publishHookMilestone(loggerTestHookState.workerDeliveryReached);
        waitForHookMilestone(loggerTestHookState.releaseWorkerDelivery);
    }

#endif
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

#if LOGGER_INTERNAL_TEST_HOOKS
namespace GameWIP::Logger::TestHooks
{
    // ------------------------------------------------------------
    // One-shot failure hooks
    // ------------------------------------------------------------

    void reset() noexcept
    {
        resetLoggerTestHooks();
    }

    void forceNextFileWriteFailure() noexcept
    {
        loggerTestHookState.nextFileWriteFailure.store(true, std::memory_order_release);
    }

    void forceNextFileFlushFailure() noexcept
    {
        loggerTestHookState.nextFileFlushFailure.store(true, std::memory_order_release);
    }

    void forceNextFatalPopupFailure() noexcept
    {
        loggerTestHookState.nextFatalPopupFailure.store(true, std::memory_order_release);
    }

    void forceNextTimedFlushTimeout() noexcept
    {
        loggerTestHookState.nextTimedFlushTimeout.store(true, std::memory_order_release);
    }

    // ------------------------------------------------------------
    // Worker delivery coordination
    // ------------------------------------------------------------

    void armWorkerDeliveryPause() noexcept
    {
        loggerTestHookState.workerDeliveryReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseWorkerDelivery.store(false, std::memory_order_release);

        loggerTestHookState.pauseBeforeWorkerDelivery.store(true, std::memory_order_release);
    }

    void waitForWorkerDeliveryPause() noexcept
    {
        waitForHookMilestone(loggerTestHookState.workerDeliveryReached);
    }

    void releaseWorkerDeliveryPause() noexcept
    {
        publishHookMilestone(loggerTestHookState.releaseWorkerDelivery);
    }

} // namespace GameWIP::Logger::TestHooks
#endif
