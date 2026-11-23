/// @file worker.cpp
/// @brief Logger queue worker and sink delivery loop.

#include "logger/internal/core.h"

#include <mutex>
#include <string>

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Worker helpers
    /// ------------------------------------------------------------

    namespace
    {
        /// @brief Returns whether the exact next ordered ring slot is ready for the worker.
        [[nodiscard]] bool queueHeadIsPublished() noexcept
        {
            const std::size_t capacity = loggerState().logRingSize;

            if(capacity == 0)
            {
                return false;
            }

            const std::size_t ticket = loggerState().dequeueTicket.load(std::memory_order_acquire);
            return loggerState().logRingView[ticket % capacity].sequence.load(std::memory_order_acquire) == ticket + 1;
        }
    } // namespace

    /// @brief Worker thread entry point that drains queued entries and writes output sinks.
    void loggerWorker()
    {
        TimestampCache timestampCache;
        std::string lineScratch;
        std::string fileBatchScratch;

        while(true)
        {
            std::size_t batchCount = 0;

            // Wait until the next ordered slot is ready or shutdown can finish.
            {
                std::unique_lock<std::mutex> lock(loggerState().logMutex);
                loggerState().logCondition.wait(
                    lock,
                    []
                    {
                        const bool ready = queueHeadIsPublished() ||
                                           (!loggerState().workerRunning && loggerState().activeProducers.load(std::memory_order_acquire) == 0 &&
                                            loggerState().queueDepth.load(std::memory_order_acquire) == 0);
                        return ready;
                    });

                if(loggerState().publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
                   loggerState().queueDepth.load(std::memory_order_acquire) == 0 &&
                   loggerState().activeProducers.load(std::memory_order_acquire) == 0 && !loggerState().workerRunning)
                {
                    break;
                }
                loggerState().workerBusy = true;
            }

            // Drain one bounded batch from the producer ring.
            batchCount = drainQueueBatch(loggerState().workerBatch);

            if(batchCount == 0)
            {
                // A producer may have reserved the queue head but not published it yet.
                {
                    std::lock_guard<std::mutex> lock(loggerState().logMutex);
                    loggerState().workerBusy = false;
                }
                loggerState().logCondition.notify_all();
                continue;
            }

#if LOGGER_INTERNAL_TEST_HOOKS
            pauseWorkerBeforeDeliveryForTest();
#endif

            std::size_t writtenCount = 0;
            std::size_t filePendingCount = 0;

            // Deliver each record, then write the accumulated file batch.
            for(std::size_t index = 0; index < batchCount; ++index)
            {
                const QueuedLogEntry &entry = loggerState().workerBatch[index];
                try
                {
                    const SinkWriteResult writeResult = writeLogEntry(entry, timestampCache, lineScratch, fileBatchScratch);

                    if(writeResult.acceptedImmediateSink)
                    {
                        ++writtenCount;
                    }
                    else if(writeResult.queuedFile)
                    {
                        ++filePendingCount;
                    }
                }
                catch(...)
                {
                    countAllocationFailure();
                }
            }

            bool fileBatchWritten = false;
            try
            {
                fileBatchWritten = flushFileBatch(fileBatchScratch, false);
            }
            catch(...)
            {
                countAllocationFailure();
            }

            if(fileBatchWritten)
            {
                writtenCount += filePendingCount;
            }

            // Release retained entries and optional excess scratch capacity.
            for(std::size_t index = 0; index < batchCount; ++index)
            {
                clearLogEntry(loggerState().workerBatch[index]);
            }

            if(loggerState().releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
            {
                std::string{}.swap(lineScratch);
                std::string{}.swap(fileBatchScratch);
            }

            // Publish batch statistics and wake flush waiters.
            {
                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                loggerState().stats.written.fetch_add(writtenCount, std::memory_order_relaxed);
                loggerState().workerBusy = false;
            }

            loggerState().logCondition.notify_all();
        }
    }

} // namespace GameWIP::Logger::Internal::Core
