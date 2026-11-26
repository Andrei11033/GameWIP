/// @file state.h
/// @brief Process-wide Logger state and sink results.

#pragma once

#include "logger/internal/queue_storage.h"
#include "logger/internal/source_registry.h"
#include "status/result.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Atomic shared-pointer storage
    /// ------------------------------------------------------------

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

    /// ------------------------------------------------------------
    /// Process-wide logger state
    /// ------------------------------------------------------------

    // Cached timestamp text
    struct TimestampCache
    {
        bool valid = false;
        std::time_t second = 0;
        std::string formatText;
        std::string text;
    };

    // Resettable statistics counters
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

    // NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding): Keep lifecycle and queue fields grouped by subsystem.
    struct LoggerState
    {
        // Severity and sink policy
        LogLevel minLevel = LogLevel::Info;
        OutputMode mode = OutputMode::Both;
        bool fallbackToConsoleOnFileFailure = true;

        // Queue and formatting limits
        std::size_t softQueueSize = 1024;
        std::size_t hardQueueSize = 1280;
        double hardQueueMultiplier = 1.25;
        std::size_t maxMessageLength = 4096;
        FormatPolicy formatPolicy = FormatPolicy::StrictBounded;
        std::size_t inlineMessageCapacity = kDefaultInlineMessageCapacity;
        std::size_t workerBatchSize = kDefaultWorkerBatchSize;
        std::size_t sourceNameWidth = 0;

        // Console layout and field limits
        bool consoleColorEnabled = true;
        Theme theme{};
        FieldLayout fieldLayout = FieldLayout::Adaptive;
        std::string timestampFormat = "%H:%M:%S.%f";
        AtomicSharedPointer<const std::string> layoutTemplate;
        std::atomic<std::size_t> maxRecordFieldsAtomic{8};
        std::atomic<std::size_t> maxFieldKeyLengthAtomic{64};
        std::atomic<std::size_t> maxFieldValueLengthAtomic{512};

        // Flush and memory policy
        bool debugOutputEnabled = true;
        bool fatalPopupEnabled = true;
        bool flushFileEveryBatch = false;
        bool flushConsoleEveryWrite = false;
        bool releaseMessageMemoryAfterWrite = false;
        bool releaseStorageOnShutdown = true;

        // Atomic runtime policy
        std::uint8_t enabledLevelMask = kAllLevelMask;
        std::atomic<std::uint32_t> runtimeStateBits{0};
        std::atomic<bool> consoleColorEnabledAtomic{true};
        std::atomic<bool> debugOutputEnabledAtomic{false};
        std::atomic<bool> fatalPopupEnabledAtomic{false};
        std::atomic<bool> fileOutputAvailableAtomic{false};
        std::atomic<bool> flushConsoleEveryWriteAtomic{false};
        std::atomic<bool> flushFileEveryBatchAtomic{false};

        // File, registry, and queue storage
        FileWriter logFile;
        FilePath logFilePath;
        AtomicSharedPointer<SourceRegistry> sourceRegistry;
        std::unique_ptr<char[]> ringMessageArena;
        std::unique_ptr<char[]> batchMessageArena;
        std::unique_ptr<QueueSlot[]> logRing;
        std::span<QueueSlot> logRingView;
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

        // Worker synchronization and lifecycle
        std::mutex lifecycleMutex;
        std::mutex logMutex;
        std::mutex outputMutex;
        std::mutex debugTimestampMutex;
        std::condition_variable logCondition;
        std::thread loggingThread;
        bool workerRunning = false;
        bool workerBusy = false;
        bool shutdownRegistered = false;

        // Runtime health and cached diagnostics
        Types::Health::State healthState = Types::Health::State::Disabled;
        Types::Health::FailureSource lastFailureSource = Types::Health::FailureSource::None;
        ErrorCode lastHealthError = ErrorCode::Success;
        std::optional<GameWIP::Status::Types::NativeCode> lastHealthNativeCode{};
        std::uint64_t healthFailureCount = 0;
        TimestampCache debugTimestampCache;
    };

    LoggerState &loggerState();

    /// @brief Tracks producer activity while a log call participates in queue admission.
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
            // Join before checking shutdown so lifecycle waits include this call.
            loggerState().activeProducers.fetch_add(1, std::memory_order_acq_rel);
            active = true;

            if((loggerState().runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
            {
                return true;
            }

            leave();
            return false;
        }

        void leave()
        {
            if(!active)
            {
                return;
            }
            active = false;

            // Wake shutdown when the final producer leaves.
            if(loggerState().activeProducers.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                loggerState().logCondition.notify_all();
            }
        }
    };

    /// ------------------------------------------------------------
    /// Sink and filtering results
    /// ------------------------------------------------------------

    // Queue admission
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

    // Output formatting
    struct LogStyle
    {
        const char *text = "UNKNOWN";
        bool useStderr = true;
    };

    // Sink delivery results
    struct SinkWriteResult
    {
        bool acceptedImmediateSink = false;
        bool queuedFile = false;
    };

    // Runtime filtering
    struct FilterDecision
    {
        bool accepted = false;
    };

    // Synchronous report delivery
    struct ReportSinkProgress
    {
        std::size_t eligible = 0;
        std::size_t delivered = 0;
        bool timedOut = false;
    };

    using ReportSinkResult = GameWIP::Status::Types::Result<ReportSinkProgress>;

#if LOGGER_INTERNAL_TEST_HOOKS
    /// ------------------------------------------------------------
    /// Test hooks
    /// ------------------------------------------------------------

    struct LoggerTestHookState
    {
        // Sink failure injection
        std::atomic_bool nextFileWriteFailure{false};
        std::atomic_bool nextFileFlushFailure{false};
        std::atomic_bool nextFatalPopupFailure{false};
        std::atomic_bool nextTimedFlushTimeout{false};

        // Worker delivery pause
        std::atomic_bool pauseBeforeWorkerDelivery{false};
        std::atomic_bool workerDeliveryReached{false};
        std::atomic_bool releaseWorkerDelivery{false};
    };
    extern LoggerTestHookState loggerTestHookState;
    bool consumeTestHook(std::atomic_bool &flag) noexcept;
    void resetLoggerTestHooks() noexcept;
    void pauseWorkerBeforeDeliveryForTest() noexcept;
    Status forcedFileStatus(ErrorCode code) noexcept;
    Status forcedFatalPopupStatus() noexcept;
#endif

} // namespace GameWIP::Logger::Internal::Core
