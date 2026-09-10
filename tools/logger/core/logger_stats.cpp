/// @file logger_stats.cpp
/// @brief Logger statistics snapshots and retained-memory accounting helpers.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    // ------------------------------------------------------------
    // Statistics and retained-memory accounting
    // ------------------------------------------------------------

    LoggerStats snapshotStats()
    {
        return LoggerStats{
            loggerState().stats.queued.load(std::memory_order_relaxed),
            loggerState().stats.written.load(std::memory_order_relaxed),
            loggerState().stats.queueDropsSoft.load(std::memory_order_relaxed),
            loggerState().stats.queueDropsHard.load(std::memory_order_relaxed),
            loggerState().stats.allocationFailures.load(std::memory_order_relaxed),
            loggerState().stats.fileWriteFailures.load(std::memory_order_relaxed),
            loggerState().stats.unknownSourceUses.load(std::memory_order_relaxed),
            loggerState().stats.formatFailures.load(std::memory_order_relaxed),
            loggerState().stats.truncated.load(std::memory_order_relaxed),
            loggerState().stats.peakQueueDepth.load(std::memory_order_relaxed)};
    }

    std::size_t messageArenaBytesUnlocked()
    {
        if (loggerState().inlineMessageCapacity == 0)
        {
            return 0;
        }

        std::size_t bytes = 0;
        if (loggerState().ringMessageArena)
        {
            bytes += loggerState().logRingSize * loggerState().inlineMessageCapacity;
        }

        if (loggerState().batchMessageArena)
        {
            bytes += loggerState().workerBatch.size() * loggerState().inlineMessageCapacity;
        }

        return bytes;
    }

    std::size_t queueStorageBytesUnlocked()
    {
        return loggerState().logRingSize * sizeof(QueueSlot) + loggerState().workerBatch.capacity() * sizeof(QueuedLogEntry);
    }

    std::size_t sourceRegistryBytes(const SourceRegistry *registry)
    {
        if (!registry)
        {
            return 0;
        }

        std::size_t bytes = sizeof(SourceRegistry) + registry->sources.capacity() * sizeof(RegisteredSource) +
                            registry->directSourceLookup.capacity() * sizeof(std::size_t);
        const std::size_t emptyCapacity = std::string{}.capacity();
        for (const auto &source : registry->sources)
        {
            if (source.name.capacity() > emptyCapacity)
            {
                bytes += source.name.capacity();
            }
        }

        return bytes;
    }

    std::size_t publishedSourceRegistryBytes()
    {
        const auto registry = loggerState().sourceRegistry.load(std::memory_order_acquire);
        return sourceRegistryBytes(registry.get());
    }

    std::size_t entryTextHeapCapacityBytes(const QueuedLogEntry &entry)
    {
        return entry.sourceText.capacityBytes() + entry.message.capacityBytes();
    }

    bool entryTextHeapCapacityAvailableUnlocked()
    {
        return !loggerState().workerBusy && loggerState().queueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState().publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState().activeProducers.load(std::memory_order_acquire) == 0;
    }

    std::size_t entryTextHeapCapacityBytesUnlocked()
    {
        if (!entryTextHeapCapacityAvailableUnlocked())
        {
            // Queue text storage can move between producer slots and the worker batch;
            // report it only when no thread can mutate those capacities.
            return 0;
        }

        std::size_t bytes = 0;
        for (std::size_t i = 0; i < loggerState().logRingSize; ++i)
        {
            bytes += entryTextHeapCapacityBytes(loggerState().logRingView[i].entry);
        }

        if (!loggerState().workerBusy)
        {
            for (const auto &entry : loggerState().workerBatch)
            {
                bytes += entryTextHeapCapacityBytes(entry);
            }
        }

        return bytes;
    }

    // ------------------------------------------------------------
    // Resettable counters
    // ------------------------------------------------------------

    void resetAtomicStats(std::size_t peakQueueDepth)
    {
        loggerState().stats.queued.store(0, std::memory_order_relaxed);
        loggerState().stats.written.store(0, std::memory_order_relaxed);
        loggerState().stats.queueDropsSoft.store(0, std::memory_order_relaxed);
        loggerState().stats.queueDropsHard.store(0, std::memory_order_relaxed);
        loggerState().stats.allocationFailures.store(0, std::memory_order_relaxed);
        loggerState().stats.fileWriteFailures.store(0, std::memory_order_relaxed);
        loggerState().stats.unknownSourceUses.store(0, std::memory_order_relaxed);
        loggerState().stats.formatFailures.store(0, std::memory_order_relaxed);
        loggerState().stats.truncated.store(0, std::memory_order_relaxed);
        loggerState().stats.peakQueueDepth.store(peakQueueDepth, std::memory_order_relaxed);
    }

    void countAllocationFailure()
    {
        recordDiagnosticFailureCounter(loggerState().stats.allocationFailures);
    }

    void countFormatFailure()
    {
        recordDiagnosticFailureCounter(loggerState().stats.formatFailures);
    }

} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

// ------------------------------------------------------------
// Public state queries and statistics
// ------------------------------------------------------------

bool GameWIP::Logger::running() noexcept
{
    return runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire));
}

GameWIP::Logger::Types::Level GameWIP::Logger::getMinLevel()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().minLevel;
}

GameWIP::Logger::Types::OutputMode GameWIP::Logger::getOutput()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().mode;
}

std::string GameWIP::Logger::getLogFilePath()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    const auto result = FileSystem::pathToUtf8(loggerState().logFilePath);
    return result.status.ok() ? result.utf8 : std::string{};
}

GameWIP::Logger::Types::QueueLimits GameWIP::Logger::getQueueLimits()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return {
        loggerState().softQueueSize,
        loggerState().hardQueueSize,
        loggerState().hardQueueMultiplier,
        loggerState().maxMessageLength,
        loggerState().inlineMessageCapacity,
        loggerState().workerBatchSize};
}

std::size_t GameWIP::Logger::getLifetimeDroppedLogCount() noexcept
{
    return loggerState().droppedLogs.load(std::memory_order_relaxed);
}

GameWIP::Logger::Types::Health::Snapshot GameWIP::Logger::getHealth()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return {
        loggerState().healthState,
        loggerState().mode,
        loggerState().lastFailureSource,
        loggerState().lastHealthError,
        loggerState().lastHealthNativeCode,
        loggerState().healthFailureCount};
}

GameWIP::Logger::Types::Stats GameWIP::Logger::getStats()
{
    return snapshotStats();
}

GameWIP::Logger::Types::MemoryStats GameWIP::Logger::getMemoryStats()
{
    LoggerMemoryStats memory;
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        memory.queueStorageBytes = queueStorageBytesUnlocked();
        memory.messageArenaBytes = messageArenaBytesUnlocked();
        memory.sourceRegistryBytes = publishedSourceRegistryBytes();
        memory.entryTextHeapCapacityAvailable = entryTextHeapCapacityAvailableUnlocked();
        memory.entryTextHeapCapacityBytes = memory.entryTextHeapCapacityAvailable ? entryTextHeapCapacityBytesUnlocked() : 0;
        memory.loggerRetainedBytes = sizeof(LoggerState) + memory.queueStorageBytes + memory.messageArenaBytes + memory.sourceRegistryBytes +
                                     memory.entryTextHeapCapacityBytes;
    }

    const auto process = GameWIP::Logger::Detail::Platform::queryProcessMemory();
    memory.processWorkingSetBytes = process.workingSetBytes;
    memory.processPrivateBytes = process.privateBytes;
    memory.processMemoryAvailable = process.available;
    return memory;
}

void GameWIP::Logger::resetStats()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    resetAtomicStats(loggerState().queueDepth.load(std::memory_order_acquire));
}
