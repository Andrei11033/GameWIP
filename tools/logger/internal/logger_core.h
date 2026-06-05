/// @file logger_core.h
/// @brief Private coordination declarations shared by Logger core translation units.

#pragma once

#include "logger/logger.h"
#include "logger/internal/logger_platform.h"

#ifndef GAMEWIP_LOGGER_TEST_HOOKS
#define GAMEWIP_LOGGER_TEST_HOOKS 0
#endif

#if GAMEWIP_LOGGER_TEST_HOOKS
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
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#endif

#ifndef LOGGER_DEFAULT_DIRECTORY
#define LOGGER_DEFAULT_DIRECTORY "logs"
#endif

namespace GameWIP::Logger::Detail::Core
{
    using LogLevel = GameWIP::Logger::Types::Level;
    using OutputMode = GameWIP::Logger::Types::Output;
    using FormatPolicy = GameWIP::Logger::Types::FormatPolicy;
    using SourceDefinition = GameWIP::Logger::Types::SourceDefinition;
    using SourceFilter = GameWIP::Logger::Types::SourceFilter;
    using LevelFilter = GameWIP::Logger::Types::LevelFilter;
    using SourceId = GameWIP::Logger::Types::SourceId;
    using LoggerResult = GameWIP::Logger::Types::Result;
    using QueueLimits = GameWIP::Logger::Types::QueueLimits;
    using LoggerStats = GameWIP::Logger::Types::Stats;
    using LoggerMemoryStats = GameWIP::Logger::Types::MemoryStats;
    using PlatformError = GameWIP::Logger::Types::PlatformError;
    using PlatformErrorSource = GameWIP::Logger::Types::PlatformErrorSource;
    using FileHandle = GameWIP::Logger::Detail::Platform::FileHandle;

    /// @brief Inline source-text capacity before falling back to heap storage.
    constexpr std::size_t kInlineSourceCapacity = 64;
    /// @brief Default per-slot message capacity reserved by Logger::Types::Config.
    constexpr std::size_t kDefaultInlineMessageCapacity = 256;
    /// @brief Number of valid Logger::Types::Level enum values.
    constexpr std::size_t kLevelCount = 6;
    /// @brief Bitmask with every valid log level enabled.
    constexpr std::uint8_t kAllLevelMask = static_cast<std::uint8_t>((1u << kLevelCount) - 1u);
    /// @brief Default number of entries the worker drains from the ring in one batch.
    constexpr std::size_t kDefaultWorkerBatchSize = 256;
    /// @brief Invalid direct source lookup index.
    constexpr std::size_t kInvalidSourceIndex = std::numeric_limits<std::size_t>::max();
    /// @brief Running bit in LoggerState::runtimeStateBits.
    constexpr std::uint32_t kRuntimeStateRunningBit = 1u << 0u;
    /// @brief Output mode bit offset in LoggerState::runtimeStateBits.
    constexpr std::uint32_t kRuntimeStateOutputShift = 1u;
    /// @brief Min-level bit offset in LoggerState::runtimeStateBits.
    constexpr std::uint32_t kRuntimeStateMinLevelShift = 4u;
    /// @brief Enabled-level mask bit offset in LoggerState::runtimeStateBits.
    constexpr std::uint32_t kRuntimeStateLevelMaskShift = 8u;
    /// @brief Mask for one packed 3-bit enum value.
    constexpr std::uint32_t kRuntimeStateEnumMask = 0x7u;
    /// @brief Mask for the packed enabled-level bits.
    constexpr std::uint32_t kRuntimeStateLevelMaskMask = 0x3Fu;
    /// @brief Number of cheap CPU-relax spins before a contended producer yields its time slice.
    constexpr std::uint32_t kQueueSlotSpinBeforeYield = 64;

    /// @brief Owns short text inline and falls back to std::string only when needed.
    template <std::size_t InlineCapacity> class InlineLogText
    {
    public:
        /// @brief Copies text into inline storage when possible, otherwise into heap storage.
        /// @param text Text to own.
        void assign(std::string_view text)
        {
            if (text.size() <= InlineCapacity)
            {
                if (heapActive)
                {
                    heapText.clear();
                }

                inlineSize = text.size();
                heapActive = false;
                if (!text.empty())
                {
                    std::memcpy(inlineText.data(), text.data(), text.size());
                }
                return;
            }

            heapText.assign(text);
            inlineSize = 0;
            heapActive = true;
        }

        /// @brief Moves owned text into heap storage when large, otherwise copies it inline.
        /// @param text Text to own.
        void assign(std::string &&text)
        {
            if (text.size() <= InlineCapacity)
            {
                assign(std::string_view(text));
                return;
            }

            heapText = std::move(text);
            inlineSize = 0;
            heapActive = true;
        }

        /// @brief Assigns two adjacent fragments without allocating when the combined text fits inline.
        /// @param first First fragment.
        /// @param second Second fragment.
        void assignJoined(std::string_view first, std::string_view second)
        {
            const std::size_t totalSize = first.size() + second.size();
            if (totalSize <= InlineCapacity)
            {
                if (heapActive)
                {
                    heapText.clear();
                }

                inlineSize = totalSize;
                heapActive = false;
                if (!first.empty())
                {
                    std::memcpy(inlineText.data(), first.data(), first.size());
                }
                if (!second.empty())
                {
                    std::memcpy(inlineText.data() + first.size(), second.data(), second.size());
                }
                return;
            }

            heapText.clear();
            heapText.reserve(totalSize);
            heapText.append(first);
            heapText.append(second);
            inlineSize = 0;
            heapActive = true;
        }

        /// @brief Transfers active text from another slot, swapping heap ownership when possible.
        /// @param source Source slot to drain.
        void transferFrom(InlineLogText &source)
        {
            if (source.heapActive)
            {
                if (heapActive)
                {
                    heapText.clear();
                }

                heapText.swap(source.heapText);
                inlineSize = 0;
                heapActive = true;

                source.heapText.clear();
                source.inlineSize = 0;
                source.heapActive = false;
                return;
            }

            assign(source.view());
            source.clear();
        }

        /// @brief Clears the logical text, optionally releasing retained heap capacity.
        /// @param releaseHeapCapacity True to release heap fallback memory instead of retaining it for reuse.
        void clear(bool releaseHeapCapacity = false)
        {
            if (heapActive)
            {
                heapText.clear();
            }
            if (releaseHeapCapacity)
            {
                std::string{}.swap(heapText);
            }

            inlineSize = 0;
            heapActive = false;
        }

        /// @brief Returns the current text without copying.
        /// @return String view valid until the next mutation of this object.
        std::string_view view() const
        {
            if (heapActive)
            {
                return heapText;
            }

            return std::string_view(inlineText.data(), inlineSize);
        }

        /// @brief Returns retained heap fallback capacity for cold memory diagnostics.
        /// @return Heap capacity retained by this text holder.
        std::size_t capacityBytes() const
        {
            static const std::size_t emptyStringCapacity = std::string{}.capacity();
            const std::size_t capacity = heapText.capacity();
            return capacity > emptyStringCapacity ? capacity : 0;
        }

    private:
        /// @brief Inline storage for short text.
        std::array<char, InlineCapacity> inlineText{};
        /// @brief Active byte count in inlineText when heapActive is false.
        std::size_t inlineSize = 0;
        /// @brief True when heapText currently owns the active text.
        bool heapActive = false;
        /// @brief Heap fallback for text longer than InlineCapacity.
        std::string heapText;
    };

    /// @brief Owns queued message text in caller-provided slot storage with heap fallback.
    class DynamicLogText
    {
    public:
        /// @brief Points this slot at its preallocated message arena bytes.
        /// @param storage Start of this slot's message storage, or nullptr when capacity is zero.
        /// @param capacity Inline message capacity chosen during Logger::init().
        void configureInlineStorage(char *storage, std::size_t capacity)
        {
            inlineText = storage;
            inlineCapacity = capacity;
            inlineSize = 0;
            clear();
        }

        /// @brief Copies text into arena storage when it fits, otherwise into heap fallback.
        /// @param text Text to own.
        void assign(std::string_view text)
        {
            if (text.size() <= inlineCapacity)
            {
                if (heapActive)
                {
                    heapText.clear();
                }

                inlineSize = text.size();
                if (!text.empty())
                {
                    std::memcpy(inlineText, text.data(), text.size());
                }
                heapActive = false;
                return;
            }

            heapText.assign(text);
            inlineSize = 0;
            heapActive = true;
        }

        /// @brief Moves text into heap fallback when large, otherwise copies it into reserved storage.
        /// @param text Text to own.
        void assign(std::string &&text)
        {
            if (text.size() <= inlineCapacity)
            {
                assign(std::string_view(text));
                return;
            }

            heapText = std::move(text);
            inlineSize = 0;
            heapActive = true;
        }

        /// @brief Assigns two adjacent fragments, using arena storage when possible.
        /// @param first First fragment.
        /// @param second Second fragment.
        void assignJoined(std::string_view first, std::string_view second)
        {
            const std::size_t totalSize = first.size() + second.size();
            if (totalSize <= inlineCapacity)
            {
                if (heapActive)
                {
                    heapText.clear();
                }

                inlineSize = totalSize;
                if (!first.empty())
                {
                    std::memcpy(inlineText, first.data(), first.size());
                }
                if (!second.empty())
                {
                    std::memcpy(inlineText + first.size(), second.data(), second.size());
                }
                heapActive = false;
                return;
            }

            heapText.clear();
            heapText.reserve(totalSize);
            heapText.append(first.data(), first.size());
            heapText.append(second.data(), second.size());
            inlineSize = 0;
            heapActive = true;
        }

        /// @brief Transfers active text from another slot, swapping heap ownership when possible.
        /// @param source Source slot to drain.
        void transferFrom(DynamicLogText &source)
        {
            if (source.heapActive)
            {
                if (heapActive)
                {
                    heapText.clear();
                }

                heapText.swap(source.heapText);
                inlineSize = 0;
                heapActive = true;

                source.heapText.clear();
                source.inlineSize = 0;
                source.heapActive = false;
                return;
            }

            assign(source.view());
            source.clear();
        }

        /// @brief Clears active text, optionally releasing reusable heap fallback capacity.
        /// @param releaseHeapCapacity True to release heap fallback memory instead of retaining it for reuse.
        void clear(bool releaseHeapCapacity = false)
        {
            if (heapActive)
            {
                heapText.clear();
            }
            if (releaseHeapCapacity)
            {
                std::string{}.swap(heapText);
            }

            inlineSize = 0;
            heapActive = false;
        }

        /// @brief Returns the active text without copying.
        /// @return String view valid until the next mutation.
        std::string_view view() const
        {
            if (heapActive)
            {
                return heapText;
            }

            if (inlineSize == 0)
            {
                return {};
            }

            return std::string_view(inlineText, inlineSize);
        }

        /// @brief Returns retained heap fallback capacity for cold memory diagnostics.
        /// @return Heap capacity retained by this text holder.
        std::size_t capacityBytes() const
        {
            static const std::size_t emptyStringCapacity = std::string{}.capacity();
            const std::size_t capacity = heapText.capacity();
            return capacity > emptyStringCapacity ? capacity : 0;
        }

    private:
        /// @brief Start of this slot's arena-backed message bytes.
        char *inlineText = nullptr;
        /// @brief Reserved normal-message bytes for this queue slot.
        std::size_t inlineCapacity = 0;
        /// @brief Active byte count in inlineText when heapActive is false.
        std::size_t inlineSize = 0;
        /// @brief True when heapText currently owns the active message.
        bool heapActive = false;
        /// @brief Fallback for messages longer than inlineCapacity.
        std::string heapText;
    };

    /// @brief Fully owned queue entry passed from producer threads to the worker thread.
    struct QueuedLogEntry
    {
        /// @brief Severity for filtering, output styling, and sink routing.
        LogLevel level = LogLevel::Info;
        /// @brief True when report paths bypass min-level and runtime filters.
        bool bypassFilters = false;
        /// @brief True when sourceId should be resolved through LoggerState::sources.
        bool usesRegisteredSource = false;
        /// @brief Registered source ID for enum/source-id log calls.
        SourceId sourceId = 0;
        /// @brief Owned source text for string source log calls.
        InlineLogText<kInlineSourceCapacity> sourceText;
        /// @brief Owned message text, truncated before queueing when needed.
        DynamicLogText message;
    };

    /// @brief Lightweight producer-side entry whose message view is copied before the log call returns.
    struct PendingLogEntry
    {
        /// @brief Severity for filtering, output styling, and sink routing.
        LogLevel level = LogLevel::Info;
        /// @brief True when report paths bypass min-level and runtime filters.
        bool bypassFilters = false;
        /// @brief True when sourceId should be resolved through LoggerState::sources.
        bool usesRegisteredSource = false;
        /// @brief Registered source ID for enum/source-id log calls.
        SourceId sourceId = 0;
        /// @brief Owned source text for string source log calls.
        InlineLogText<kInlineSourceCapacity> sourceText;
        /// @brief Message text to copy into the selected ring slot.
        std::string_view message;
        /// @brief True when formatting already applied the truncation suffix.
        bool alreadyTruncated = false;
    };

    /// @brief One MPSC ring slot with a sequence number for publish/consume coordination.
    struct QueueSlot
    {
        /// @brief Slot sequence. Producers publish ticket + 1; consumer releases ticket + capacity.
        std::atomic<std::size_t> sequence{0};
        /// @brief True when a claimed slot failed to build and should only unblock the consumer.
        bool skip = false;
        /// @brief Owned queued entry.
        QueuedLogEntry entry;

        QueueSlot() = default;
        QueueSlot(const QueueSlot &) = delete;
        QueueSlot &operator=(const QueueSlot &) = delete;
        QueueSlot(QueueSlot &&) = delete;
        QueueSlot &operator=(QueueSlot &&) = delete;
    };

    /// @brief Registered sources are immutable after init except for the atomic enabled flag.
    struct RegisteredSource
    {
        /// @brief Registered source ID.
        SourceId id = 0;
        /// @brief Copied display name written in log lines.
        std::string name;
        /// @brief Runtime source filter flag.
        mutable std::atomic<bool> enabled{true};

        /// @brief Creates an empty source entry for vector storage.
        RegisteredSource() = default;

        /// @brief Creates a registered source entry.
        /// @param source Registered source ID.
        /// @param displayName Copied display name.
        /// @param sourceEnabled Initial runtime filter state.
        RegisteredSource(SourceId source, std::string displayName, bool sourceEnabled)
            : id(source)
            , name(std::move(displayName))
            , enabled(sourceEnabled)
        {
        }

        /// @brief Copies source metadata and atomically snapshots the enabled flag.
        /// @param other Source entry to copy.
        RegisteredSource(const RegisteredSource &other)
            : id(other.id)
            , name(other.name)
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        /// @brief Moves source metadata and atomically snapshots the enabled flag.
        /// @param other Source entry to move.
        RegisteredSource(RegisteredSource &&other) noexcept
            : id(other.id)
            , name(std::move(other.name))
            , enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        /// @brief Copies source metadata and atomically snapshots the enabled flag.
        /// @param other Source entry to copy.
        /// @return This source entry.
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

        /// @brief Moves source metadata and atomically snapshots the enabled flag.
        /// @param other Source entry to move.
        /// @return This source entry.
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

    /// @brief Immutable source lookup table published atomically for lifecycle-safe producer reads.
    struct SourceRegistry
    {
        /// @brief Sorted registered source table.
        std::vector<RegisteredSource> sources;
        /// @brief Direct source lookup for dense source ID ranges.
        std::vector<std::size_t> directSourceLookup;
        /// @brief First SourceId represented by directSourceLookup.
        SourceId directSourceBase = 0;
    };

    /// @brief Cached formatted local time for one second.
    struct TimestampCache
    {
        /// @brief True once text contains a valid cached timestamp.
        bool valid = false;
        /// @brief Whole-second time represented by text.
        std::time_t second = 0;
        /// @brief Formatted timestamp text.
        std::string text;
    };

    /// @brief Resettable counters stored as relaxed atomics for hot-path accounting.
    struct StatsCounters
    {
        /// @brief Messages accepted into the async queue.
        std::atomic<std::size_t> queued{0};
        /// @brief Messages accepted by at least one enabled output sink.
        std::atomic<std::size_t> written{0};
        /// @brief Low-priority messages dropped at the soft queue limit.
        std::atomic<std::size_t> queueDropsSoft{0};
        /// @brief Messages dropped at the hard queue limit.
        std::atomic<std::size_t> queueDropsHard{0};
        /// @brief Messages dropped because allocation or internal formatting failed.
        std::atomic<std::size_t> allocationFailures{0};
        /// @brief File write or flush failures observed while other sinks keep running.
        std::atomic<std::size_t> fileWriteFailures{0};
        /// @brief Processed queued entries that used unregistered SourceId values.
        std::atomic<std::size_t> unknownSourceUses{0};
        /// @brief Runtime format strings that failed validation in std::vformat.
        std::atomic<std::size_t> formatFailures{0};
        /// @brief Messages truncated to Config::maxMessageLength.
        std::atomic<std::size_t> truncated{0};
        /// @brief Highest observed queue depth since init or resetStats().
        std::atomic<std::size_t> peakQueueDepth{0};
    };

    /// @brief All mutable logger state owned by the logger library.
    struct LoggerState
    {
        /// @brief Startup severity floor.
        LogLevel minLevel = LogLevel::Info;
        /// @brief Current output mode, including file setup fallback.
        OutputMode mode = OutputMode::Both;
        /// @brief Allows Output::File to fall back to Console when file setup fails.
        bool fallbackToConsoleOnFileFailure = true;
        /// @brief Soft queue limit for low-priority drops.
        std::size_t softQueueSize = 1024;
        /// @brief Hard queue limit where every severity may drop.
        std::size_t hardQueueSize = 1280;
        /// @brief Sanitized multiplier used to derive hardQueueSize before rounding/fallback.
        double hardQueueMultiplier = 1.25;
        /// @brief Maximum stored message length.
        std::size_t maxMessageLength = 4096;
        /// @brief Formatting memory/speed policy used by header-only format helpers.
        FormatPolicy formatPolicy = FormatPolicy::StrictBounded;
        /// @brief Per-slot normal message storage capacity.
        std::size_t inlineMessageCapacity = kDefaultInlineMessageCapacity;
        /// @brief Worker entries drained per batch.
        std::size_t workerBatchSize = kDefaultWorkerBatchSize;
        /// @brief Console color setting mirrored to consoleColorEnabledAtomic.
        bool consoleColorEnabled = true;
        /// @brief Debug-output setting mirrored to debugOutputEnabledAtomic.
        bool debugOutputEnabled = true;
        /// @brief Fatal popup setting mirrored to fatalPopupEnabledAtomic.
        bool fatalPopupEnabled = true;
        /// @brief File flush policy mirrored to flushFileEveryBatchAtomic.
        bool flushFileEveryBatch = false;
        /// @brief Console flush policy mirrored to flushConsoleEveryWriteAtomic.
        bool flushConsoleEveryWrite = false;
        /// @brief Releases heap fallback text after queue entries are cleared.
        bool releaseMessageMemoryAfterWrite = false;
        /// @brief Releases queue, batch, arena, and source-registry storage during shutdown.
        bool releaseStorageOnShutdown = true;

        /// @brief Exact-level runtime filter bitmask protected by logMutex.
        std::uint8_t enabledLevelMask = kAllLevelMask;
        /// @brief Packed running/output/min-level/level-mask state used by producer hot paths.
        std::atomic<std::uint32_t> runtimeStateBits{0};
        /// @brief Atomic console color setting used by worker output.
        std::atomic<bool> consoleColorEnabledAtomic{true};
        /// @brief True when stdout is an ANSI-capable console and color was requested.
        std::atomic<bool> stdoutColorEnabledAtomic{false};
        /// @brief True when stderr is an ANSI-capable console and color was requested.
        std::atomic<bool> stderrColorEnabledAtomic{false};
        /// @brief Atomic platform debug output setting used by direct platform debug output calls.
        std::atomic<bool> debugOutputEnabledAtomic{false};
        /// @brief Atomic fatal popup setting used by reportFatal().
        std::atomic<bool> fatalPopupEnabledAtomic{false};
        /// @brief Atomic file availability flag set only when the file sink is open.
        std::atomic<bool> fileOutputAvailableAtomic{false};
        /// @brief Atomic console flush policy used by worker output.
        std::atomic<bool> flushConsoleEveryWriteAtomic{false};
        /// @brief Atomic file flush policy used by worker output.
        std::atomic<bool> flushFileEveryBatchAtomic{false};

        /// @brief Active file sink owned behind outputMutex.
        FileHandle logFile;
        /// @brief Current file sink path.
        std::filesystem::path logFilePath;
        /// @brief Atomically published registered source table used by registered source hot paths.
        std::atomic<std::shared_ptr<SourceRegistry>> sourceRegistry{};
        /// @brief Uninitialized arena bytes backing message storage for logRing.
        std::unique_ptr<char[]> ringMessageArena;
        /// @brief Uninitialized arena bytes backing message storage for workerBatch.
        std::unique_ptr<char[]> batchMessageArena;
        /// @brief Preallocated contiguous MPSC ring buffer sized to hardQueueSize.
        std::unique_ptr<QueueSlot[]> logRing;
        /// @brief Number of slots owned by logRing.
        std::size_t logRingSize = 0;
        /// @brief Reusable worker batch storage.
        std::vector<QueuedLogEntry> workerBatch;
        /// @brief Next ticket claimed by a producer.
        std::atomic<std::size_t> enqueueTicket{0};
        /// @brief Next ticket expected by the single worker.
        std::atomic<std::size_t> dequeueTicket{0};
        /// @brief Reserved/published queue depth, including skip markers not yet drained.
        std::atomic<std::size_t> queueDepth{0};
        /// @brief Published entries/skip markers available for the worker to drain.
        std::atomic<std::size_t> publishedQueueDepth{0};
        /// @brief Producers inside the enqueue path; used to close shutdown races without a producer mutex.
        std::atomic<std::size_t> activeProducers{0};
        /// @brief Maximum message length mirrored for header-only bounded formatting.
        std::atomic<std::size_t> maxMessageLengthAtomic{4096};
        /// @brief Formatting policy mirrored for header-only formatting.
        std::atomic<std::uint8_t> formatPolicyAtomic{0};
        /// @brief True when thread-local and worker scratch buffers should release peak capacity.
        std::atomic<bool> releaseMessageMemoryAfterWriteAtomic{false};

        /// @brief Lifetime queue-drop count preserved for external reporting.
        std::atomic<std::size_t> droppedLogs{0};
        /// @brief Visible stats counters reset by resetStats().
        StatsCounters stats;

        /// @brief Serializes public lifecycle operations and public flush calls.
        /// @details Internal flush helpers do not take this lock so shutdown can drain while already lifecycle-locked.
        std::mutex lifecycleMutex;
        /// @brief Protects lifecycle/config state, source-registry publication, flush coordination, and last result.
        std::mutex logMutex;
        /// @brief Protects console and file stream writes.
        std::mutex outputMutex;
        /// @brief Protects debugTimestampCache for direct platform debug output calls.
        std::mutex debugTimestampMutex;
        /// @brief Coordinates producer, flush, shutdown, and worker wait/wake behavior.
        std::condition_variable logCondition;
        /// @brief Single async consumer thread.
        std::thread loggingThread;
        /// @brief Worker loop state protected by logMutex.
        bool workerRunning = false;
        /// @brief True while the worker is processing a drained batch.
        bool workerBusy = false;
        /// @brief Ensures atexit cleanup is registered only once.
        bool shutdownRegistered = false;

        /// @brief Last result reported by init, runtime filters, or sink failures.
        LoggerResult lastResult = LoggerResult::Success;
        /// @brief Last structured platform error.
        PlatformError lastPlatformError;
        /// @brief Timestamp cache for direct platform debug output calls outside the worker.
        TimestampCache debugTimestampCache;
    };

    LoggerState &loggerState();

    /// @brief Marks one producer as active while it may reserve or publish a queue slot.
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

        /// @brief Enters the producer path only while the logger is still running.
        /// @return True when the caller may continue into filter checks and queue reservation.
        bool enter()
        {
            loggerState().activeProducers.fetch_add(1, std::memory_order_acq_rel);
            active = true;
            if ((loggerState().runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
            {
                return true;
            }

            leave();
            return false;
        }

        /// @brief Leaves the producer path and wakes shutdown/worker waiters when this was the last producer.
        void leave()
        {
            if (!active)
            {
                return;
            }

            active = false;
            if (loggerState().activeProducers.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                loggerState().logCondition.notify_all();
            }
        }
    };

    /// @brief Output text, console color, and stream route for one severity level.
    struct LogStyle
    {
        /// @brief Short level label written into each log line.
        const char *text = "UNKNOWN";
        /// @brief ANSI color prefix for console output.
        const char *color = "";
        /// @brief True routes console output to stderr; false routes to stdout.
        bool useCerr = true;
    };

    /// @brief Queue insertion result used by queue-pressure accounting and report fallback.
    enum class EnqueueStatus
    {
        Skipped,
        Queued,
        DroppedSoft,
        DroppedHard,
        AllocationFailure
    };

    /// @brief Queue insertion side effects returned to the producer after unlocking.
    struct EnqueueOutcome
    {
        /// @brief True when the producer should wake the worker.
        bool notifyWorker = false;
        /// @brief Final enqueue result used by report fallback handling.
        EnqueueStatus status = EnqueueStatus::Skipped;
    };

    /// @brief Output acceptance state for one worker-written entry.
    struct SinkWriteResult
    {
        /// @brief True when console output accepted the entry immediately.
        bool acceptedImmediateSink = false;
        /// @brief True when file output has queued the entry into the file batch.
        bool queuedFile = false;
    };

    /// @brief Result of rechecking filters while holding logMutex.
    struct FilterDecision
    {
        /// @brief True when the entry should still be queued. Filtered entries are silent skips.
        bool accepted = false;
    };

    // Process state and test hooks.

#if GAMEWIP_LOGGER_TEST_HOOKS
    struct LoggerTestHookState
    {
        std::atomic_bool nextFileOpenFailure{false};
        std::atomic_bool nextFileWriteFailure{false};
        std::atomic_bool nextFileFlushFailure{false};
        std::atomic_bool nextQueueAllocationFailure{false};
        std::atomic_bool nextFatalPopupFailure{false};
        std::atomic_bool nextTimedFlushTimeout{false};
    };
    extern LoggerTestHookState loggerTestHookState;
    bool consumeTestHook(std::atomic_bool &flag) noexcept;
    void resetLoggerTestHooks() noexcept;
    PlatformError forcedFileError() noexcept;
    PlatformError forcedFatalPopupError() noexcept;
#endif

    // Enum conversion and result accounting.

    std::uint8_t toLevelValue(LogLevel level);
    std::uint8_t toOutputModeValue(OutputMode mode);
    std::uint8_t toFormatPolicyValue(FormatPolicy policy);
    FormatPolicy formatPolicyFromValue(std::uint8_t value);
    FormatPolicy sanitizeFormatPolicy(FormatPolicy policy);
    bool isValidLevel(LogLevel level);
    bool isValidOutputMode(OutputMode mode);
    std::uint8_t levelBit(LogLevel level);
    OutputMode outputModeFromValue(std::uint8_t mode);
    std::uint32_t packRuntimeState(bool running, OutputMode mode, LogLevel minLevel, std::uint8_t levelMask);
    bool runtimeStateRunning(std::uint32_t packed);
    OutputMode runtimeStateOutput(std::uint32_t packed);
    LogLevel runtimeStateMinLevel(std::uint32_t packed);
    std::uint8_t runtimeStateLevelMask(std::uint32_t packed);
    bool hasPlatformError(const PlatformError &error);
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
    void setResultUnlocked(LoggerResult result, PlatformError platformError = {});
    void recordResult(LoggerResult result, PlatformError platformError = {});
    void recordPlatformErrorIfAny(const PlatformError &platformError);
    void preserveFirstInitResult(
        LoggerResult &inOutResult,
        LoggerResult candidateResult,
        PlatformError &inOutPlatformError,
        PlatformError candidatePlatformError = {});
    void recordFileWriteFailure(PlatformError platformError = {});
    void countAllocationFailure();
    void countFormatFailure();
    void publishRuntimeStateUnlocked();
    void publishConsoleColorSupport();
    bool consoleColorEnabledForStream(bool useCerr);

    // Source registration and runtime filters.

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

    // Queue and runtime storage setup.

    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier);
    std::size_t effectiveHardQueueLimit(double &inOutHardQueueMultiplier, std::size_t softQueueSize, LoggerResult &inOutResult);
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
        LoggerResult &outResult);
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

    // Formatting, source resolution, and sink writes.

    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, PlatformError &outError);
    std::string getCurrentTimeText(std::string_view timeFormat, PlatformError &outError);
    std::string_view getTimestampText(TimestampCache &cache);
    std::string getDebugTimestampText();
    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource);
    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource);
    void recordUnknownSourceUse();
    void assignMessage(QueuedLogEntry &entry, std::string_view message, std::size_t maxMessageLength, bool &outTruncated);
    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength);
    std::string_view boundedMessageView(std::string_view message, bool alreadyTruncated, std::string &scratch, bool &outTruncated);
    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message);
    PlatformError openFileExclusiveForLogger(std::string_view path, FileHandle &outHandle);
    PlatformError writeFileForLogger(FileHandle handle, std::string_view text);
    PlatformError flushFileForLogger(FileHandle handle);
    bool writeReportSynchronously(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool unknownSource = false,
        bool alreadyTruncated = false);
    bool writeReportSynchronously(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated = false);
    SinkWriteResult writeLogEntry(
        const QueuedLogEntry &entry,
        TimestampCache &timestampCache,
        std::string &lineScratch,
        std::string &fileBatchScratch);
    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush);
    void loggerWorker();
    void shutdownLoggerAtExit();

    // Lifecycle state transitions and enqueue/report bridge helpers.

    void resetRuntimeStateUnlocked(
        const GameWIP::Logger::Types::Config &config,
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
    void setOutputMode(OutputMode mode);
    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole);
    EnqueueOutcome enqueuePendingLogEntry(const PendingLogEntry &entry, bool countDrops = true);
    PendingLogEntry makePendingEntry(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool bypassFilters = false,
        bool alreadyTruncated = false);
    PendingLogEntry makePendingEntry(
        LogLevel level,
        SourceId source,
        std::string_view message,
        bool bypassFilters = false,
        bool alreadyTruncated = false);
    EnqueueOutcome enqueueAndWakeWorker(const PendingLogEntry &entry, bool countDrops = true);
    void showFatalPopupIfEnabled(std::string_view message);
    bool flushSinksInternal();
    void flushInternal();
    bool flushInternal(std::chrono::milliseconds timeout);
} // namespace GameWIP::Logger::Detail::Core
