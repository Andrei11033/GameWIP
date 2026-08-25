/// @file logger_queue.cpp
/// @brief Bounded MPSC queue storage, ordered publication, producer admission, and asynchronous worker draining.

#include "logger/internal/logger_core.h"
#include "base/checked_arithmetic.h"

namespace GameWIP::Logger::Detail::Core
{
    /// Allocates overflow-checked contiguous inline message storage for queue entries.
    bool allocateMessageArena(std::size_t entryCount, std::size_t inlineMessageCapacity, std::unique_ptr<char[]> &arena)
    {
        arena.reset();
        if (entryCount == 0 || inlineMessageCapacity == 0)
        {
            return true;
        }

        if (GameWIP::Base::wouldMultiplyOverflow(entryCount, inlineMessageCapacity))
        {
            return false;
        }

        arena.reset(new char[entryCount * inlineMessageCapacity]);
        return true;
    }

    /// Builds complete ring and worker storage before initialization commits runtime state.
    /// Failure clears every output so partially initialized queue storage cannot escape.
    bool prepareQueueStorage(
        std::size_t hardLimit,
        std::size_t workerBatchSize,
        std::size_t inlineMessageCapacity,
        std::unique_ptr<QueueSlot[]> &ring,
        std::size_t &ringSize,
        std::vector<QueuedLogEntry> &batch,
        std::unique_ptr<char[]> &ringArena,
        std::unique_ptr<char[]> &batchArena)
    {
        try
        {
            ring.reset();
            ringSize = 0;
            batch.clear();
            ringArena.reset();
            batchArena.reset();

            if (GameWIP::Base::wouldMultiplyOverflow(hardLimit, sizeof(QueueSlot)))
            {
                return false;
            }

            ring = std::make_unique<QueueSlot[]>(hardLimit);
            ringSize = hardLimit;
            batch.resize(workerBatchSize);
            if (!allocateMessageArena(ringSize, inlineMessageCapacity, ringArena) ||
                !allocateMessageArena(batch.size(), inlineMessageCapacity, batchArena))
            {
                ring.reset();
                ringSize = 0;
                batch.clear();
                ringArena.reset();
                batchArena.reset();
                return false;
            }

            for (std::size_t index = 0; index < ringSize; ++index)
            {
                char *storage = inlineMessageCapacity == 0 ? nullptr : ringArena.get() + (index * inlineMessageCapacity);
                ring[index].entry.message.configureInlineStorage(storage, inlineMessageCapacity);
                ring[index].sequence.store(index, std::memory_order_relaxed);
                ring[index].skip = false;
            }
            for (std::size_t index = 0; index < batch.size(); ++index)
            {
                char *storage = inlineMessageCapacity == 0 ? nullptr : batchArena.get() + (index * inlineMessageCapacity);
                batch[index].message.configureInlineStorage(storage, inlineMessageCapacity);
            }
            return true;
        }
        catch (...)
        {
            ring.reset();
            ringSize = 0;
            batch.clear();
            ringArena.reset();
            batchArena.reset();
            return false;
        }
    }

    //-------------------------------------------------------------------------------------------------
    // Queue entry storage helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Clears queued entries while preserving allocated queue and batch capacity.
    /// @pre loggerState().logMutex is held.
    void clearQueueUnlocked()
    {
        const bool releaseHeapCapacity = loggerState().releaseMessageMemoryAfterWrite;
        for (std::size_t index = 0; index < loggerState().logRingSize; ++index)
        {
            QueueSlot &slot = loggerState().logRing[index];
            QueuedLogEntry &entry = slot.entry;
            entry.usesRegisteredSource = false;
            entry.sourceId = 0;
            entry.sourceText.clear(releaseHeapCapacity);
            entry.message.clear(releaseHeapCapacity);
            slot.skip = false;
            slot.sequence.store(index, std::memory_order_relaxed);
        }

        for (QueuedLogEntry &entry : loggerState().workerBatch)
        {
            entry.usesRegisteredSource = false;
            entry.sourceId = 0;
            entry.sourceText.clear(releaseHeapCapacity);
            entry.message.clear(releaseHeapCapacity);
        }
        loggerState().enqueueTicket.store(0, std::memory_order_relaxed);
        loggerState().dequeueTicket.store(0, std::memory_order_relaxed);
        loggerState().queueDepth.store(0, std::memory_order_relaxed);
        loggerState().publishedQueueDepth.store(0, std::memory_order_relaxed);
    }

    /// @brief Releases retained queue, batch, arena, and source-registry storage.
    /// @pre loggerState().logMutex is held and no worker/producers are mutating queue storage.
    void releaseRuntimeStorageUnlocked()
    {
        loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        loggerState().ringMessageArena.reset();
        loggerState().batchMessageArena.reset();
        loggerState().logRing.reset();
        loggerState().logRingSize = 0;
        std::vector<QueuedLogEntry>{}.swap(loggerState().workerBatch);
        loggerState().softQueueSize = 0;
        loggerState().hardQueueSize = 0;
        loggerState().workerBatchSize = 0;
        loggerState().inlineMessageCapacity = 0;
    }

    /// Clears logical entry state while applying the configured heap-retention policy.
    void clearLogEntry(QueuedLogEntry &entry)
    {
        const bool releaseHeapCapacity = loggerState().releaseMessageMemoryAfterWrite;
        entry.level = LogLevel::Info;
        entry.usesRegisteredSource = false;
        entry.sourceId = 0;
        entry.sourceText.clear(releaseHeapCapacity);
        entry.message.clear(releaseHeapCapacity);
    }

    /// Copies only the retained prefix and suffix, avoiding a temporary full-size string.
    void assignRetainedMessage(QueuedLogEntry &entry, std::string_view message, std::size_t maxMessageLength, bool &outTruncated)
    {
        constexpr std::string_view suffix = "... [truncated]";
        outTruncated = false;
        if (message.size() <= maxMessageLength)
        {
            entry.message.assign(message);
            return;
        }

        outTruncated = true;
        if (maxMessageLength == 0)
        {
            entry.message.clear();
            return;
        }
        if (maxMessageLength <= suffix.size())
        {
            entry.message.assign(suffix.substr(0, maxMessageLength));
            return;
        }

        entry.message.assignJoined(message.substr(0, maxMessageLength - suffix.size()), suffix);
    }

    /// Materializes a non-owning producer entry in queue-owned storage.
    void copyPendingEntryToQueueSlot(QueuedLogEntry &destination, const PendingLogEntry &source, bool &outTruncated)
    {
        destination.level = source.level;
        destination.usesRegisteredSource = source.usesRegisteredSource;
        destination.sourceId = source.sourceId;
        if (source.usesRegisteredSource)
        {
            destination.sourceText.clear();
        }
        else
        {
            destination.sourceText.assign(source.sourceText.view());
        }
        if (source.alreadyTruncated)
        {
            destination.message.assign(source.message);
            outTruncated = true;
        }
        else
        {
            assignRetainedMessage(destination, source.message, loggerState().maxMessageLength, outTruncated);
        }
    }

    /// Transfers queue-owned text, swapping heap storage when either slot overflowed inline capacity.
    void moveQueuedEntry(QueuedLogEntry &destination, QueuedLogEntry &source)
    {
        destination.level = source.level;
        destination.usesRegisteredSource = source.usesRegisteredSource;
        destination.sourceId = source.sourceId;
        if (source.usesRegisteredSource)
        {
            destination.sourceText.clear();
        }
        else
        {
            destination.sourceText.transferFrom(source.sourceText);
        }
        destination.message.transferFrom(source.message);
    }

    /// @brief Attempts to reserve queue depth according to soft/hard drop policy.
    /// @param entry Pending entry to classify.
    /// @param outPreviousDepth Receives the queue depth before this reservation.
    /// @return Queue policy result; Queued means a slot depth was reserved.
    EnqueueStatus reserveQueueDepth(const PendingLogEntry &entry, std::size_t &outPreviousDepth)
    {
        std::size_t depth = loggerState().queueDepth.load(std::memory_order_acquire);
        while (true)
        {
            if (depth >= loggerState().hardQueueSize)
            {
                return EnqueueStatus::DroppedHard;
            }
            if (depth >= loggerState().softQueueSize && isLowPriority(entry.level))
            {
                return EnqueueStatus::DroppedSoft;
            }
            if (loggerState().queueDepth.compare_exchange_weak(depth, depth + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                outPreviousDepth = depth;
                return EnqueueStatus::Queued;
            }
        }
    }

    /// @brief Publishes a filled or skip-marked slot and wakes the worker when it was sleeping.
    /// @details Failed producers still publish a skip marker so ticket order cannot leave an unfillable hole that blocks every later record.
    /// @param slot Slot to publish.
    /// @param ticket Producer ticket for this slot.
    /// @param outNotifyWorker Receives true when publication may make the queue head drainable.
    void publishQueueSlot(QueueSlot &slot, std::size_t ticket, bool &outNotifyWorker)
    {
        slot.sequence.store(ticket + 1, std::memory_order_release);
        const bool firstPublishedSlot = loggerState().publishedQueueDepth.fetch_add(1, std::memory_order_acq_rel) == 0;
        outNotifyWorker = firstPublishedSlot || ticket == loggerState().dequeueTicket.load(std::memory_order_acquire);
#if LOGGER_INTERNAL_TEST_HOOKS
        recordQueuePublicationForTest();
#endif
    }

    /// @brief Publishes one pending entry into a reserved MPSC ring slot.
    /// @param entry Pending entry to enqueue.
    /// @param outTruncated Receives true when stored message text was truncated.
    /// @param outNotifyWorker Receives true when the worker should be woken for newly published work.
    /// @return Queued on success, or AllocationFailure after publishing a skip marker.
    EnqueueStatus publishReservedQueueEntry(const PendingLogEntry &entry, bool &outTruncated, bool &outNotifyWorker)
    {
        outNotifyWorker = false;
        const std::size_t capacity = loggerState().logRingSize;
        if (capacity == 0)
        {
            loggerState().queueDepth.fetch_sub(1, std::memory_order_acq_rel);
            return EnqueueStatus::DroppedHard;
        }

        const std::size_t ticket = loggerState().enqueueTicket.fetch_add(1, std::memory_order_acq_rel);
        QueueSlot &slot = loggerState().logRing[ticket % capacity];
        waitForQueueSlot(slot, ticket);

        try
        {
#if LOGGER_INTERNAL_TEST_HOOKS
            if (consumeTestHook(loggerTestHookState.nextQueueAllocationFailure))
            {
                throw std::bad_alloc{};
            }
#endif
            slot.skip = false;
            copyPendingEntryToQueueSlot(slot.entry, entry, outTruncated);
        }
        catch (...)
        {
            slot.skip = true;
            clearLogEntry(slot.entry);
            publishQueueSlot(slot, ticket, outNotifyWorker);
            return EnqueueStatus::AllocationFailure;
        }

        publishQueueSlot(slot, ticket, outNotifyWorker);
        return EnqueueStatus::Queued;
    }

    /// @brief Drains a bounded batch from the MPSC ring in ticket order.
    /// @param batch Worker-owned reusable batch vector.
    std::size_t drainQueueBatch(std::vector<QueuedLogEntry> &batch)
    {
        std::size_t batchCount = 0;
        const std::size_t capacity = loggerState().logRingSize;
        const std::size_t batchLimit = batch.empty() ? capacity : batch.size();
        if (capacity == 0 || batchLimit == 0)
        {
            return 0;
        }

        while (batchCount < batchLimit)
        {
            const std::size_t ticket = loggerState().dequeueTicket.load(std::memory_order_relaxed);
            QueueSlot &slot = loggerState().logRing[ticket % capacity];
            if (slot.sequence.load(std::memory_order_acquire) != ticket + 1)
            {
                break;
            }

            if (!slot.skip)
            {
                moveQueuedEntry(batch[batchCount], slot.entry);
                ++batchCount;
            }

            slot.skip = false;
            clearLogEntry(slot.entry);
            slot.sequence.store(ticket + capacity, std::memory_order_release);
            loggerState().dequeueTicket.store(ticket + 1, std::memory_order_release);
            loggerState().publishedQueueDepth.fetch_sub(1, std::memory_order_acq_rel);
            loggerState().queueDepth.fetch_sub(1, std::memory_order_acq_rel);
        }

        return batchCount;
    }

    //-------------------------------------------------------------------------------------------------
    // Worker helpers
    //-------------------------------------------------------------------------------------------------

    namespace
    {
        /// @brief Returns whether the exact next ordered ring slot is ready for the worker.
        [[nodiscard]] bool queueHeadIsPublished() noexcept
        {
            const std::size_t capacity = loggerState().logRingSize;
            if (capacity == 0)
            {
                return false;
            }

            const std::size_t ticket = loggerState().dequeueTicket.load(std::memory_order_acquire);
            return loggerState().logRing[ticket % capacity].sequence.load(std::memory_order_acquire) == ticket + 1;
        }
    } // namespace

    /// @brief Worker thread entry point that drains queued entries and writes output sinks.
    void loggerWorker()
    {
        TimestampCache timestampCache;
        std::string lineScratch;
        std::string fileBatchScratch;

        while (true)
        {
            std::size_t batchCount = 0;
            {
                std::unique_lock<std::mutex> lock(loggerState().logMutex);
                loggerState().logCondition.wait(
                    lock,
                    []
                    {
                        const bool ready = queueHeadIsPublished() ||
                                           (!loggerState().workerRunning && loggerState().activeProducers.load(std::memory_order_acquire) == 0 &&
                                            loggerState().queueDepth.load(std::memory_order_acquire) == 0);
#if LOGGER_INTERNAL_TEST_HOOKS
                        if (!ready)
                        {
                            pauseWorkerBeforeWaitForTest();
                        }
#endif
                        return ready;
                    });

                if (loggerState().publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
                    loggerState().queueDepth.load(std::memory_order_acquire) == 0 &&
                    loggerState().activeProducers.load(std::memory_order_acquire) == 0 && !loggerState().workerRunning)
                {
                    break;
                }
                loggerState().workerBusy = true;
            }

            batchCount = drainQueueBatch(loggerState().workerBatch);
            if (batchCount == 0)
            {
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
            for (std::size_t index = 0; index < batchCount; ++index)
            {
                const QueuedLogEntry &entry = loggerState().workerBatch[index];
                try
                {
                    const SinkWriteResult writeResult = writeLogEntry(entry, timestampCache, lineScratch, fileBatchScratch);
                    if (writeResult.acceptedImmediateSink)
                    {
                        ++writtenCount;
                    }
                    else if (writeResult.queuedFile)
                    {
                        ++filePendingCount;
                    }
                }
                catch (...)
                {
                    countAllocationFailure();
                }
            }

            bool fileBatchWritten = false;
            try
            {
                fileBatchWritten = flushFileBatch(fileBatchScratch, false);
            }
            catch (...)
            {
                countAllocationFailure();
            }

            if (fileBatchWritten)
            {
                writtenCount += filePendingCount;
            }
            for (std::size_t index = 0; index < batchCount; ++index)
            {
                clearLogEntry(loggerState().workerBatch[index]);
            }

            if (loggerState().releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
            {
                std::string{}.swap(lineScratch);
                std::string{}.swap(fileBatchScratch);
            }

            {
                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                loggerState().stats.written.fetch_add(writtenCount, std::memory_order_relaxed);
                loggerState().workerBusy = false;
            }

            loggerState().logCondition.notify_all();
        }
    }

    /// @brief atexit callback that delegates to the idempotent public shutdown path.
    void shutdownLoggerAtExit()
    {
        GameWIP::Logger::shutdown();
    }

    /// @brief Applies queue policy and enqueues one pending entry if accepted.
    /// @param entry Pending entry to enqueue.
    /// @return Post-unlock side effects for worker wake.
    /// @note Queue policy intentionally drops every severity at hardQueueSize.
    EnqueueOutcome enqueuePendingLogEntry(const PendingLogEntry &entry, bool countDrops)
    {
        EnqueueOutcome result;
        ProducerActivity producer;
        if (!producer.enter())
        {
            return result;
        }

        const FilterDecision filterCheck = checkPendingEntryAcceptedUnlocked(entry);
        if (!filterCheck.accepted)
        {
            return result;
        }

        std::size_t previousDepth = 0;
        const EnqueueStatus reserveStatus = reserveQueueDepth(entry, previousDepth);
        if (reserveStatus == EnqueueStatus::DroppedHard)
        {
            if (countDrops)
            {
                recordQueueDropCounter(loggerState().stats.queueDropsHard);
            }
            result.status = EnqueueStatus::DroppedHard;
            return result;
        }

        if (reserveStatus == EnqueueStatus::DroppedSoft)
        {
            if (countDrops)
            {
                recordQueueDropCounter(loggerState().stats.queueDropsSoft);
            }
            result.status = EnqueueStatus::DroppedSoft;
            return result;
        }

        bool entryWasTruncated = false;
        bool notifyWorker = false;
        const EnqueueStatus publishStatus = publishReservedQueueEntry(entry, entryWasTruncated, notifyWorker);
        if (publishStatus == EnqueueStatus::AllocationFailure)
        {
            if (countDrops)
            {
                countAllocationFailure();
            }
            result.status = EnqueueStatus::AllocationFailure;
            result.notifyWorker = notifyWorker;
            return result;
        }

        if (publishStatus == EnqueueStatus::DroppedHard)
        {
            if (countDrops)
            {
                recordQueueDropCounter(loggerState().stats.queueDropsHard);
            }
            result.status = EnqueueStatus::DroppedHard;
            return result;
        }

        loggerState().stats.queued.fetch_add(1, std::memory_order_relaxed);
        if (entryWasTruncated)
        {
            loggerState().stats.truncated.fetch_add(1, std::memory_order_relaxed);
        }

        updateAtomicMax(loggerState().stats.peakQueueDepth, previousDepth + 1);
        result.notifyWorker = notifyWorker;
        result.status = EnqueueStatus::Queued;
        return result;
    }

    /// @brief Builds a borrowed-message pending entry with a string source.
    /// @param level Entry severity.
    /// @param source Source text to copy into the pending entry.
    /// @param message Message view copied into the ring slot before the public call returns.
    /// @return Pending producer-side entry.
    PendingLogEntry makePendingEntry(LogLevel level, std::string_view source, std::string_view message, bool alreadyTruncated)
    {
        PendingLogEntry entry;
        entry.level = level;
        entry.usesRegisteredSource = false;
        entry.sourceId = 0;
        entry.sourceText.assign(source);
        entry.message = message;
        entry.alreadyTruncated = alreadyTruncated;
        return entry;
    }

    /// @brief Builds a borrowed-message pending entry with a registered SourceId.
    /// @param level Entry severity.
    /// @param source Registered SourceId to store.
    /// @param message Message view copied into the ring slot before the public call returns.
    /// @return Pending producer-side entry.
    PendingLogEntry makePendingEntry(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated)
    {
        PendingLogEntry entry;
        entry.level = level;
        entry.usesRegisteredSource = true;
        entry.sourceId = source;
        entry.sourceText.clear();
        entry.message = message;
        entry.alreadyTruncated = alreadyTruncated;
        return entry;
    }

    /// @brief Enqueues a pending entry and wakes the worker when needed.
    /// @param entry Pending entry whose borrowed message remains valid for this call.
    EnqueueOutcome enqueueAndWakeWorker(const PendingLogEntry &entry, bool countDrops)
    {
        const EnqueueOutcome enqueueResult = enqueuePendingLogEntry(entry, countDrops);

        if (enqueueResult.notifyWorker)
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().logCondition.notify_all();
        }

        return enqueueResult;
    }
} // namespace GameWIP::Logger::Detail::Core
