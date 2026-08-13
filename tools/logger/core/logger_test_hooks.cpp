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

    bool consumeTestHook(std::atomic_bool &flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }

    void resetLoggerTestHooks() noexcept
    {
        loggerTestHookState.nextFileOpenFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileWriteFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileFlushFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextQueueAllocationFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFatalPopupFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextTimedFlushTimeout.store(false, std::memory_order_release);
        loggerTestHookState.pauseBeforeWorkerWait.store(false, std::memory_order_release);
        loggerTestHookState.workerWaitReached.store(false, std::memory_order_release);
        loggerTestHookState.queuePublicationReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseWorkerWait.store(false, std::memory_order_release);
        loggerTestHookState.pauseBeforeFinalProducerLeave.store(false, std::memory_order_release);
        loggerTestHookState.finalProducerLeaveReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseFinalProducerLeave.store(false, std::memory_order_release);
        loggerTestHookState.pauseBeforeWorkerDelivery.store(false, std::memory_order_release);
        loggerTestHookState.workerDeliveryReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseWorkerDelivery.store(false, std::memory_order_release);
        loggerTestHookState.lifecycleLockReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseLifecycleLock.store(false, std::memory_order_release);
    }

    void pauseWorkerBeforeWaitForTest() noexcept
    {
        if (!consumeTestHook(loggerTestHookState.pauseBeforeWorkerWait))
        {
            return;
        }

        publishHookMilestone(loggerTestHookState.workerWaitReached);
        waitForHookMilestone(loggerTestHookState.releaseWorkerWait);
    }

    void recordQueuePublicationForTest() noexcept
    {
        publishHookMilestone(loggerTestHookState.queuePublicationReached);
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

    void pauseFinalProducerLeaveForTest() noexcept
    {
        if (!consumeTestHook(loggerTestHookState.pauseBeforeFinalProducerLeave))
        {
            return;
        }

        publishHookMilestone(loggerTestHookState.finalProducerLeaveReached);
        waitForHookMilestone(loggerTestHookState.releaseFinalProducerLeave);
    }
#endif
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

#if INTERNAL_LOGGER_TEST_HOOKS
namespace GameWIP::Logger::TestHooks
{
    void reset() noexcept
    {
        resetLoggerTestHooks();
    }

    void forceNextFileOpenFailure() noexcept
    {
        loggerTestHookState.nextFileOpenFailure.store(true, std::memory_order_release);
    }

    void forceNextFileWriteFailure() noexcept
    {
        loggerTestHookState.nextFileWriteFailure.store(true, std::memory_order_release);
    }

    void forceNextFileFlushFailure() noexcept
    {
        loggerTestHookState.nextFileFlushFailure.store(true, std::memory_order_release);
    }

    void forceNextQueueAllocationFailure() noexcept
    {
        loggerTestHookState.nextQueueAllocationFailure.store(true, std::memory_order_release);
    }

    void forceNextFatalPopupFailure() noexcept
    {
        loggerTestHookState.nextFatalPopupFailure.store(true, std::memory_order_release);
    }

    void forceNextTimedFlushTimeout() noexcept
    {
        loggerTestHookState.nextTimedFlushTimeout.store(true, std::memory_order_release);
    }

    void armWorkerWaitPause() noexcept
    {
        loggerTestHookState.workerWaitReached.store(false, std::memory_order_release);
        loggerTestHookState.queuePublicationReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseWorkerWait.store(false, std::memory_order_release);
        loggerTestHookState.pauseBeforeWorkerWait.store(true, std::memory_order_release);
    }

    void waitForWorkerWaitPause() noexcept
    {
        waitForHookMilestone(loggerTestHookState.workerWaitReached);
    }

    void waitForQueuePublication() noexcept
    {
        waitForHookMilestone(loggerTestHookState.queuePublicationReached);
    }

    void releaseWorkerWaitPause() noexcept
    {
        publishHookMilestone(loggerTestHookState.releaseWorkerWait);
    }

    void armFinalProducerLeavePause() noexcept
    {
        loggerTestHookState.finalProducerLeaveReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseFinalProducerLeave.store(false, std::memory_order_release);
        loggerTestHookState.pauseBeforeFinalProducerLeave.store(true, std::memory_order_release);
    }

    void waitForFinalProducerLeavePause() noexcept
    {
        waitForHookMilestone(loggerTestHookState.finalProducerLeaveReached);
    }

    void releaseFinalProducerLeavePause() noexcept
    {
        publishHookMilestone(loggerTestHookState.releaseFinalProducerLeave);
    }

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

    void holdLifecycleLockPause() noexcept
    {
        loggerTestHookState.lifecycleLockReached.store(false, std::memory_order_release);
        loggerTestHookState.releaseLifecycleLock.store(false, std::memory_order_release);
        std::lock_guard lock(loggerState().lifecycleMutex);
        publishHookMilestone(loggerTestHookState.lifecycleLockReached);
        waitForHookMilestone(loggerTestHookState.releaseLifecycleLock);
    }

    void waitForLifecycleLockPause() noexcept
    {
        waitForHookMilestone(loggerTestHookState.lifecycleLockReached);
    }

    void releaseLifecycleLockPause() noexcept
    {
        publishHookMilestone(loggerTestHookState.releaseLifecycleLock);
    }

} // namespace GameWIP::Logger::TestHooks
#endif
