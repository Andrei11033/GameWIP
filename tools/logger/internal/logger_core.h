/// @file logger_core.h
/// @brief Private Logger state and core contracts.

#pragma once

#include "logger/logger.h"
#include "logger/internal/logger_platform.h"

#include "filesystem/filesystem.h"
#include "io/io.h"
#include "terminal/terminal.h"
#include "unicode/unicode.h"

#ifndef LOGGER_INTERNAL_TEST_HOOKS
#define LOGGER_INTERNAL_TEST_HOOKS 0
#endif
#if LOGGER_INTERNAL_TEST_HOOKS
#include "logger/internal/logger_test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace GameWIP::Logger::Detail::Core
{
    using FlushDeadline = std::chrono::steady_clock::time_point;
    using LogLevel = Types::Level;
    using OutputMode = Types::OutputMode;
    using FormatPolicy = Types::FormatPolicy;
    using SourceDefinition = Types::SourceDefinition;
    using SourceFilter = Types::SourceFilter;
    using LevelFilter = Types::LevelFilter;
    using SourceId = Types::SourceId;
    using QueueLimits = Types::QueueLimits;
    using LoggerStats = Types::Stats;
    using LoggerMemoryStats = Types::MemoryStats;
    using Status = IO::Types::Status;
    using ErrorCode = IO::Types::ErrorCode;
    using FileWriter = FileSystem::FileWriter;
    using FilePath = FileSystem::Types::Path;

    constexpr std::size_t kInlineSourceCapacity = 64;
    constexpr std::size_t kDefaultInlineMessageCapacity = 256;
    constexpr std::size_t kLevelCount = 6;
    constexpr std::uint8_t kAllLevelMask = static_cast<std::uint8_t>((1u << kLevelCount) - 1u);
    constexpr std::size_t kDefaultWorkerBatchSize = 256;
    constexpr std::size_t kInvalidSourceIndex = std::numeric_limits<std::size_t>::max();
    constexpr std::uint32_t kRuntimeStateRunningBit = 1u << 0u;
    constexpr std::uint32_t kRuntimeStateOutputShift = 1u;
    constexpr std::uint32_t kRuntimeStateMinLevelShift = 4u;
    constexpr std::uint32_t kRuntimeStateLevelMaskShift = 8u;
    constexpr std::uint32_t kRuntimeStateEnumMask = 0x7u;
    constexpr std::uint32_t kRuntimeStateLevelMaskMask = 0x3Fu;
    constexpr std::uint32_t kQueueSlotSpinBeforeYield = 64;

    /// @brief Returns the byte count that keeps a valid UTF-8 prefix when a borrowed view may end mid-scalar.
    /// @note Ordinary hot messages are valid UTF-8 by contract, so only the final scalar needs inspection here.
    [[nodiscard]] inline std::size_t completeUtf8TailBoundary(std::string_view text) noexcept
    {
        if (text.empty())
            return 0;

        std::size_t lead = text.size() - 1;
        std::size_t continuationCount = 0;
        while (lead > 0 && continuationCount < 3)
        {
            const auto value = static_cast<unsigned char>(text[lead]);
            if ((value & 0xC0u) != 0x80u)
                break;
            --lead;
            ++continuationCount;
        }

        const auto first = static_cast<unsigned char>(text[lead]);
        std::size_t expected = 1;
        if (first >= 0xC2u && first <= 0xDFu)
            expected = 2;
        else if (first >= 0xE0u && first <= 0xEFu)
            expected = 3;
        else if (first >= 0xF0u && first <= 0xF4u)
            expected = 4;
        else
            return text.size();

        const std::size_t available = text.size() - lead;
        return available < expected ? lead : text.size();
    }

    template <std::size_t InlineCapacity> class InlineLogText
    {
    public:
        void assign(std::string_view text)
        {
            if (text.size() <= InlineCapacity)
            {
                if (heapActive_)
                    heapText_.clear();
                inlineSize_ = text.size();
                heapActive_ = false;
                if (!text.empty())
                    std::memcpy(inlineText_.data(), text.data(), text.size());
                return;
            }
            heapText_.assign(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }
        void assign(std::string &&text)
        {
            if (text.size() <= InlineCapacity)
            {
                assign(std::string_view(text));
                return;
            }
            heapText_ = std::move(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }
        void assignJoined(std::string_view first, std::string_view second)
        {
            first = first.substr(0, completeUtf8TailBoundary(first));
            const std::size_t total = first.size() + second.size();
            if (total <= InlineCapacity)
            {
                if (heapActive_)
                    heapText_.clear();
                inlineSize_ = total;
                heapActive_ = false;
                if (!first.empty())
                    std::memcpy(inlineText_.data(), first.data(), first.size());
                if (!second.empty())
                    std::memcpy(inlineText_.data() + first.size(), second.data(), second.size());
                return;
            }
            heapText_.clear();
            heapText_.reserve(total);
            heapText_.append(first);
            heapText_.append(second);
            inlineSize_ = 0;
            heapActive_ = true;
        }
        void transferFrom(InlineLogText &source)
        {
            if (source.heapActive_)
            {
                if (heapActive_)
                    heapText_.clear();
                heapText_.swap(source.heapText_);
                inlineSize_ = 0;
                heapActive_ = true;
                source.heapText_.clear();
                source.inlineSize_ = 0;
                source.heapActive_ = false;
                return;
            }
            assign(source.view());
            source.clear();
        }
        void clear(bool releaseHeapCapacity = false)
        {
            if (heapActive_)
                heapText_.clear();
            if (releaseHeapCapacity)
                std::string{}.swap(heapText_);
            inlineSize_ = 0;
            heapActive_ = false;
        }
        [[nodiscard]] std::string_view view() const
        {
            return heapActive_ ? std::string_view(heapText_) : std::string_view(inlineText_.data(), inlineSize_);
        }
        [[nodiscard]] std::size_t capacityBytes() const
        {
            static const std::size_t emptyCapacity = std::string{}.capacity();
            return heapText_.capacity() > emptyCapacity ? heapText_.capacity() : 0;
        }

    private:
        std::array<char, InlineCapacity> inlineText_{};
        std::size_t inlineSize_ = 0;
        bool heapActive_ = false;
        std::string heapText_;
    };

    class DynamicLogText
    {
    public:
        void configureInlineStorage(char *storage, std::size_t capacity)
        {
            inlineText_ = storage;
            inlineCapacity_ = capacity;
            inlineSize_ = 0;
            clear();
        }
        void assign(std::string_view text)
        {
            if (text.size() <= inlineCapacity_)
            {
                if (heapActive_)
                    heapText_.clear();
                inlineSize_ = text.size();
                if (!text.empty())
                    std::memcpy(inlineText_, text.data(), text.size());
                heapActive_ = false;
                return;
            }
            heapText_.assign(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }
        void assign(std::string &&text)
        {
            if (text.size() <= inlineCapacity_)
            {
                assign(std::string_view(text));
                return;
            }
            heapText_ = std::move(text);
            inlineSize_ = 0;
            heapActive_ = true;
        }
        void assignJoined(std::string_view first, std::string_view second)
        {
            first = first.substr(0, completeUtf8TailBoundary(first));
            const std::size_t total = first.size() + second.size();
            if (total <= inlineCapacity_)
            {
                if (heapActive_)
                    heapText_.clear();
                inlineSize_ = total;
                if (!first.empty())
                    std::memcpy(inlineText_, first.data(), first.size());
                if (!second.empty())
                    std::memcpy(inlineText_ + first.size(), second.data(), second.size());
                heapActive_ = false;
                return;
            }
            heapText_.clear();
            heapText_.reserve(total);
            heapText_.append(first.data(), first.size());
            heapText_.append(second.data(), second.size());
            inlineSize_ = 0;
            heapActive_ = true;
        }
        void transferFrom(DynamicLogText &source)
        {
            if (source.heapActive_)
            {
                if (heapActive_)
                    heapText_.clear();
                heapText_.swap(source.heapText_);
                inlineSize_ = 0;
                heapActive_ = true;
                source.heapText_.clear();
                source.inlineSize_ = 0;
                source.heapActive_ = false;
                return;
            }
            assign(source.view());
            source.clear();
        }
        void clear(bool releaseHeapCapacity = false)
        {
            if (heapActive_)
                heapText_.clear();
            if (releaseHeapCapacity)
                std::string{}.swap(heapText_);
            inlineSize_ = 0;
            heapActive_ = false;
        }
        [[nodiscard]] std::string_view view() const
        {
            if (heapActive_)
                return heapText_;
            return inlineSize_ == 0 ? std::string_view{} : std::string_view(inlineText_, inlineSize_);
        }
        [[nodiscard]] std::size_t capacityBytes() const
        {
            static const std::size_t emptyCapacity = std::string{}.capacity();
            return heapText_.capacity() > emptyCapacity ? heapText_.capacity() : 0;
        }

    private:
        char *inlineText_ = nullptr;
        std::size_t inlineCapacity_ = 0;
        std::size_t inlineSize_ = 0;
        bool heapActive_ = false;
        std::string heapText_;
    };

    struct QueuedLogEntry
    {
        LogLevel level = LogLevel::Info;
        bool usesRegisteredSource = false;
        SourceId sourceId = 0;
        InlineLogText<kInlineSourceCapacity> sourceText;
        DynamicLogText message;
    };

    struct PendingLogEntry
    {
        LogLevel level = LogLevel::Info;
        bool usesRegisteredSource = false;
        SourceId sourceId = 0;
        InlineLogText<kInlineSourceCapacity> sourceText;
        std::string_view message;
        bool alreadyTruncated = false;
    };

    struct QueueSlot
    {
        std::atomic<std::size_t> sequence{0};
        bool skip = false;
        QueuedLogEntry entry;
        QueueSlot() = default;
        QueueSlot(const QueueSlot &) = delete;
        QueueSlot &operator=(const QueueSlot &) = delete;
        QueueSlot(QueueSlot &&) = delete;
        QueueSlot &operator=(QueueSlot &&) = delete;
    };

    struct RegisteredSource
    {
        SourceId id = 0;
        std::string name;
        mutable std::atomic<bool> enabled{true};
        RegisteredSource() = default;
        RegisteredSource(SourceId source, std::string displayName, bool sourceEnabled)
            : id(source)
            , name(std::move(displayName))
            , enabled(sourceEnabled)
        {
        }
        RegisteredSource(const RegisteredSource &other)
            : id(other.id)
            , name(other.name)
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }
        RegisteredSource(RegisteredSource &&other) noexcept
            : id(other.id)
            , name(std::move(other.name))
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }
        RegisteredSource &operator=(const RegisteredSource &other)
        {
            if (this != &other)
            {
                id = other.id;
                name = other.name;
                enabled.store(other.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return *this;
        }
        RegisteredSource &operator=(RegisteredSource &&other) noexcept
        {
            if (this != &other)
            {
                id = other.id;
                name = std::move(other.name);
                enabled.store(other.enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return *this;
        }
    };

    struct SourceRegistry
    {
        std::vector<RegisteredSource> sources;
        std::vector<std::size_t> directSourceLookup;
        SourceId directSourceBase = 0;
    };

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    template <typename Value> using AtomicSharedPointer = std::atomic<std::shared_ptr<Value>>;
#else
    template <typename Value> class AtomicSharedPointer
    {
    public:
        [[nodiscard]] std::shared_ptr<Value> load(std::memory_order order) const
        {
            return std::atomic_load_explicit(&value_, order);
        }
        void store(std::shared_ptr<Value> value, std::memory_order order)
        {
            std::atomic_store_explicit(&value_, std::move(value), order);
        }

    private:
        std::shared_ptr<Value> value_;
    };
#endif

    struct TimestampCache
    {
        bool valid = false;
        std::time_t second = 0;
        std::string text;
    };

    struct StatsCounters
    {
        std::atomic<std::size_t> queued{0};
        std::atomic<std::size_t> written{0};
        std::atomic<std::size_t> queueDropsSoft{0};
        std::atomic<std::size_t> queueDropsHard{0};
        std::atomic<std::size_t> allocationFailures{0};
        std::atomic<std::size_t> fileWriteFailures{0};
        std::atomic<std::size_t> unknownSourceUses{0};
        std::atomic<std::size_t> formatFailures{0};
        std::atomic<std::size_t> truncated{0};
        std::atomic<std::size_t> peakQueueDepth{0};
    };

    struct LoggerState
    {
        LogLevel minLevel = LogLevel::Info;
        OutputMode mode = OutputMode::Both;
        bool fallbackToConsoleOnFileFailure = true;
        std::size_t softQueueSize = 1024;
        std::size_t hardQueueSize = 1280;
        double hardQueueMultiplier = 1.25;
        std::size_t maxMessageLength = 4096;
        FormatPolicy formatPolicy = FormatPolicy::StrictBounded;
        std::size_t inlineMessageCapacity = kDefaultInlineMessageCapacity;
        std::size_t workerBatchSize = kDefaultWorkerBatchSize;
        bool consoleColorEnabled = true;
        bool debugOutputEnabled = true;
        bool fatalPopupEnabled = true;
        bool flushFileEveryBatch = false;
        bool flushConsoleEveryWrite = false;
        bool releaseMessageMemoryAfterWrite = false;
        bool releaseStorageOnShutdown = true;

        std::uint8_t enabledLevelMask = kAllLevelMask;
        std::atomic<std::uint32_t> runtimeStateBits{0};
        std::atomic<bool> consoleColorEnabledAtomic{true};
        std::atomic<bool> debugOutputEnabledAtomic{false};
        std::atomic<bool> fatalPopupEnabledAtomic{false};
        std::atomic<bool> fileOutputAvailableAtomic{false};
        std::atomic<bool> flushConsoleEveryWriteAtomic{false};
        std::atomic<bool> flushFileEveryBatchAtomic{false};

        FileWriter logFile;
        FilePath logFilePath;
        AtomicSharedPointer<SourceRegistry> sourceRegistry;
        std::unique_ptr<char[]> ringMessageArena;
        std::unique_ptr<char[]> batchMessageArena;
        std::unique_ptr<QueueSlot[]> logRing;
        std::size_t logRingSize = 0;
        std::vector<QueuedLogEntry> workerBatch;
        std::atomic<std::size_t> enqueueTicket{0};
        std::atomic<std::size_t> dequeueTicket{0};
        std::atomic<std::size_t> queueDepth{0};
        std::atomic<std::size_t> publishedQueueDepth{0};
        std::atomic<std::size_t> activeProducers{0};
        std::atomic<std::size_t> maxMessageLengthAtomic{4096};
        std::atomic<std::uint8_t> formatPolicyAtomic{0};
        std::atomic<bool> releaseMessageMemoryAfterWriteAtomic{false};
        std::atomic<std::size_t> droppedLogs{0};
        StatsCounters stats;

        std::mutex lifecycleMutex;
        std::mutex logMutex;
        std::mutex outputMutex;
        std::mutex debugTimestampMutex;
        std::condition_variable logCondition;
        std::thread loggingThread;
        bool workerRunning = false;
        bool workerBusy = false;
        bool shutdownRegistered = false;

        Types::Health::State healthState = Types::Health::State::Disabled;
        Types::Health::FailureSource lastFailureSource = Types::Health::FailureSource::None;
        ErrorCode lastHealthError = ErrorCode::Success;
        std::int64_t lastHealthNativeCode = 0;
        std::uint64_t healthFailureCount = 0;
        TimestampCache debugTimestampCache;
    };

    LoggerState &loggerState();

#if LOGGER_INTERNAL_TEST_HOOKS
    void pauseFinalProducerLeaveForTest() noexcept;
#endif

    struct ProducerActivity
    {
        bool active = false;
        ProducerActivity() = default;
        ProducerActivity(const ProducerActivity &) = delete;
        ProducerActivity &operator=(const ProducerActivity &) = delete;
        ~ProducerActivity()
        {
            leave();
        }
        bool enter()
        {
            loggerState().activeProducers.fetch_add(1, std::memory_order_acq_rel);
            active = true;
            if ((loggerState().runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
                return true;
            leave();
            return false;
        }
        void leave()
        {
            if (!active)
                return;
            active = false;
#if LOGGER_INTERNAL_TEST_HOOKS
            pauseFinalProducerLeaveForTest();
#endif
            if (loggerState().activeProducers.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                loggerState().logCondition.notify_all();
            }
        }
    };

    struct LogStyle
    {
        const char *text = "UNKNOWN";
        Terminal::Types::Style::Request terminalStyle{};
        bool useStderr = true;
    };

    enum class EnqueueStatus
    {
        Skipped,
        Queued,
        DroppedSoft,
        DroppedHard,
        AllocationFailure
    };
    struct EnqueueOutcome
    {
        bool notifyWorker = false;
        EnqueueStatus status = EnqueueStatus::Skipped;
    };
    struct SinkWriteResult
    {
        bool acceptedImmediateSink = false;
        bool queuedFile = false;
    };
    struct FilterDecision
    {
        bool accepted = false;
    };

    struct ReportSinkProgress
    {
        Status status;
        std::size_t eligible = 0;
        std::size_t delivered = 0;
        bool timedOut = false;
    };

#if LOGGER_INTERNAL_TEST_HOOKS
    struct LoggerTestHookState
    {
        std::atomic_bool nextFileOpenFailure{false};
        std::atomic_bool nextFileWriteFailure{false};
        std::atomic_bool nextFileFlushFailure{false};
        std::atomic_bool nextQueueAllocationFailure{false};
        std::atomic_bool nextFatalPopupFailure{false};
        std::atomic_bool nextTimedFlushTimeout{false};
        std::atomic_bool pauseBeforeWorkerWait{false};
        std::atomic_bool workerWaitReached{false};
        std::atomic_bool queuePublicationReached{false};
        std::atomic_bool releaseWorkerWait{false};
        std::atomic_bool pauseBeforeFinalProducerLeave{false};
        std::atomic_bool finalProducerLeaveReached{false};
        std::atomic_bool releaseFinalProducerLeave{false};
        std::atomic_bool pauseBeforeWorkerDelivery{false};
        std::atomic_bool workerDeliveryReached{false};
        std::atomic_bool releaseWorkerDelivery{false};
        std::atomic_bool lifecycleLockReached{false};
        std::atomic_bool releaseLifecycleLock{false};
    };
    extern LoggerTestHookState loggerTestHookState;
    bool consumeTestHook(std::atomic_bool &flag) noexcept;
    void resetLoggerTestHooks() noexcept;
    void pauseWorkerBeforeWaitForTest() noexcept;
    void pauseWorkerBeforeDeliveryForTest() noexcept;
    void recordQueuePublicationForTest() noexcept;
    Status forcedFileStatus(ErrorCode code) noexcept;
    Status forcedFatalPopupStatus() noexcept;
#endif

    std::uint8_t toLevelValue(LogLevel level);
    std::uint8_t toOutputModeValue(OutputMode mode);
    std::uint8_t toFormatPolicyValue(FormatPolicy policy);
    FormatPolicy formatPolicyFromValue(std::uint8_t value);
    bool isValidFormatPolicy(FormatPolicy policy);
    bool isValidLevel(LogLevel level);
    bool isValidOutputMode(OutputMode mode);
    std::uint8_t levelBit(LogLevel level);
    OutputMode outputModeFromValue(std::uint8_t value);
    std::uint32_t packRuntimeState(bool running, OutputMode mode, LogLevel minLevel, std::uint8_t levelMask);
    bool runtimeStateRunning(std::uint32_t packed);
    OutputMode runtimeStateOutput(std::uint32_t packed);
    LogLevel runtimeStateMinLevel(std::uint32_t packed);
    std::uint8_t runtimeStateLevelMask(std::uint32_t packed);
    void cpuRelax() noexcept;
    void waitForQueueSlot(QueueSlot &slot, std::size_t ticket);
    void recordQueueDropCounter(std::atomic<std::size_t> &counter);
    void recordDiagnosticFailureCounter(std::atomic<std::size_t> &counter);
    void updateAtomicMax(std::atomic<std::size_t> &target, std::size_t value);
    LoggerStats snapshotStats();
    std::size_t messageArenaBytesUnlocked();
    std::size_t queueStorageBytesUnlocked();
    std::size_t sourceRegistryBytes(const SourceRegistry *registry);
    std::size_t publishedSourceRegistryBytes();
    std::size_t entryTextHeapCapacityBytes(const QueuedLogEntry &entry);
    bool entryTextHeapCapacityAvailableUnlocked();
    std::size_t entryTextHeapCapacityBytesUnlocked();
    void resetAtomicStats(std::size_t peakQueueDepth = 0);
    void waitForActiveProducersToLeave();
    void countAllocationFailure();
    void countFormatFailure();
    void publishRuntimeStateUnlocked();

    void resetHealthUnlocked(Types::Health::State state, OutputMode effectiveOutput);
    void recordHealthFailure(Types::Health::FailureSource source, const Status &status, bool disableChannel);
    void markHealthDisabledUnlocked();
    [[nodiscard]] Status firstFailure(Status current, const Status &candidate);

    std::shared_ptr<SourceRegistry> loadSourceRegistry();
    RegisteredSource *findSource(SourceRegistry &registry, SourceId source);
    const RegisteredSource *findSource(const SourceRegistry &registry, SourceId source);
    void rebuildSourceLookup(SourceRegistry &registry);
    bool sourceEnabledRuntime(SourceId source);
    bool sourceEnabledRuntime(const SourceRegistry *registry, SourceId source);
    bool shouldLogRuntime(LogLevel level);
    bool shouldLogRuntime(LogLevel level, SourceId source);
    FilterDecision checkPendingEntryAcceptedUnlocked(const PendingLogEntry &entry);
    LogStyle getLogStyle(LogLevel level);
    bool isLowPriority(LogLevel level);
    bool hasConsoleOutput(OutputMode mode);
    bool hasFileOutput(OutputMode mode);

    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier);
    std::size_t effectiveHardQueueLimit(double &multiplier, std::size_t softQueueSize, bool &adjusted);
    std::size_t effectiveWorkerBatchSize(std::size_t requested, std::size_t hardLimit);
    bool allocateMessageArena(std::size_t entryCount, std::size_t inlineMessageCapacity, std::unique_ptr<char[]> &arena);
    bool prepareQueueStorage(
        std::size_t hardLimit,
        std::size_t workerBatchSize,
        std::size_t inlineMessageCapacity,
        std::unique_ptr<QueueSlot[]> &ring,
        std::size_t &ringSize,
        std::vector<QueuedLogEntry> &batch,
        std::unique_ptr<char[]> &ringArena,
        std::unique_ptr<char[]> &batchArena);
    bool prepareSources(
        std::span<const SourceDefinition> definitions,
        std::span<const SourceFilter> filters,
        std::shared_ptr<SourceRegistry> &registry,
        Status &outStatus);
    bool prepareLevelMask(std::span<const LevelFilter> filters, std::uint8_t &outMask);
    void clearQueueUnlocked();
    void releaseRuntimeStorageUnlocked();
    void clearLogEntry(QueuedLogEntry &entry);
    void assignRetainedMessage(QueuedLogEntry &entry, std::string_view message, std::size_t maxMessageLength, bool &outTruncated);
    void copyPendingEntryToQueueSlot(QueuedLogEntry &destination, const PendingLogEntry &source, bool &outTruncated);
    void moveQueuedEntry(QueuedLogEntry &destination, QueuedLogEntry &source);
    EnqueueStatus reserveQueueDepth(const PendingLogEntry &entry, std::size_t &outPreviousDepth);
    void publishQueueSlot(QueueSlot &slot, std::size_t ticket, bool &outNotifyWorker);
    EnqueueStatus publishReservedQueueEntry(const PendingLogEntry &entry, bool &outTruncated, bool &outNotifyWorker);
    std::size_t drainQueueBatch(std::vector<QueuedLogEntry> &batch);

    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, Status &outStatus);
    std::string getCurrentTimeText(std::string_view timeFormat, Status &outStatus);
    std::string_view getTimestampText(TimestampCache &cache);
    std::string getDebugTimestampText(Status *outStatus = nullptr);
    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource);
    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource);
    void recordUnknownSourceUse();
    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength);
    std::string_view boundedMessageView(std::string_view message, bool alreadyTruncated, std::string &scratch, bool &outTruncated);
    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message);

    Status openFileExclusiveForLogger(const FilePath &path, FileWriter &outWriter);
    Status writeFileForLogger(FileWriter &writer, std::string_view text);
    Status flushFileForLogger(FileWriter &writer);
    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool unknownSource = false,
        bool alreadyTruncated = false,
        const FlushDeadline *deadline = nullptr);
    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        SourceId source,
        std::string_view message,
        bool alreadyTruncated = false,
        const FlushDeadline *deadline = nullptr);
    SinkWriteResult writeLogEntry(
        const QueuedLogEntry &entry,
        TimestampCache &timestampCache,
        std::string &lineScratch,
        std::string &fileBatchScratch);
    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush);
    void loggerWorker();
    void shutdownLoggerAtExit();

    void resetRuntimeStateUnlocked(
        const Types::Config &config,
        std::size_t softQueueSize,
        std::size_t hardQueueSize,
        double hardQueueMultiplier,
        std::size_t messageLength,
        std::size_t inlineMessageCapacity,
        std::size_t workerBatchSize,
        std::uint8_t levelMask,
        std::shared_ptr<SourceRegistry> sourceRegistry,
        std::unique_ptr<QueueSlot[]> &&ring,
        std::size_t ringSize,
        std::vector<QueuedLogEntry> &&batch,
        std::unique_ptr<char[]> &&ringArena,
        std::unique_ptr<char[]> &&batchArena);
    Types::Init::Result initImpl(const Types::Config &config);
    Status shutdownImpl();
    void setOutputMode(OutputMode mode);
    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole);
    EnqueueOutcome enqueuePendingLogEntry(const PendingLogEntry &entry, bool countDrops = true);
    PendingLogEntry makePendingEntry(LogLevel level, std::string_view source, std::string_view message, bool alreadyTruncated = false);
    PendingLogEntry makePendingEntry(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated = false);
    EnqueueOutcome enqueueAndWakeWorker(const PendingLogEntry &entry, bool countDrops = true);

    Types::Report::Result reportPreformattedMessageImpl(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool showPopup,
        bool alreadyTruncated,
        const std::chrono::milliseconds *timeout,
        bool unknownSource = false);
    Types::FlushResult flushSinksInternal(const FlushDeadline *deadline = nullptr);
    Types::FlushResult flushInternal(const FlushDeadline *deadline = nullptr);
    [[nodiscard]] FlushDeadline makeFlushDeadline(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] bool lockBefore(std::unique_lock<std::mutex> &lock, FlushDeadline deadline);
} // namespace GameWIP::Logger::Detail::Core
