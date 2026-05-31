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
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#endif

#if defined(_WIN32) && defined(__MINGW32__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef LOGGER_DEFAULT_DIRECTORY
/// @brief Fallback log directory used when the build system does not provide a project-root path.
#define LOGGER_DEFAULT_DIRECTORY "logs"
#endif

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
using FileHandle = GameWIP::LoggerDetail::Platform::FileHandle;

namespace
{
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

#if GAMEWIP_LOGGER_TEST_HOOKS
    /// @brief One-shot failure injection flags used only by logger tests.
    struct LoggerTestHookState
    {
        std::atomic_bool nextFileOpenFailure{false};
        std::atomic_bool nextFileWriteFailure{false};
        std::atomic_bool nextFileFlushFailure{false};
        std::atomic_bool nextQueueAllocationFailure{false};
        std::atomic_bool nextFatalPopupFailure{false};
        std::atomic_bool nextTimedFlushTimeout{false};
    };

    LoggerTestHookState loggerTestHookState;

    /// @brief Consumes a one-shot test-hook flag.
    /// @param flag Hook flag to clear.
    /// @return True when the hook was armed before this call.
    bool consumeTestHook(std::atomic_bool &flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }

    /// @brief Clears all logger test hooks.
    void resetLoggerTestHooks() noexcept
    {
        loggerTestHookState.nextFileOpenFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileWriteFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFileFlushFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextQueueAllocationFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextFatalPopupFailure.store(false, std::memory_order_release);
        loggerTestHookState.nextTimedFlushTimeout.store(false, std::memory_order_release);
    }
#endif

#if defined(_WIN32) && defined(__MINGW32__)
    /// @brief Frees MinGW format scratch without using non-trivial C++ TLS destructors.
    void NTAPI destroyFormatScratch(void *value)
    {
        delete static_cast<std::string *>(value);
    }

    /// @brief Returns the FLS slot used for per-thread format scratch on MinGW.
    DWORD formatScratchSlot()
    {
        static const DWORD slot = FlsAlloc(destroyFormatScratch);
        return slot;
    }

    /// @brief Returns MinGW per-thread format scratch through FLS.
    /// @return Mutable per-thread scratch string.
    std::string &formatScratchForThread()
    {
        const DWORD slot = formatScratchSlot();
        if (slot == FLS_OUT_OF_INDEXES)
        {
            throw std::bad_alloc();
        }

        auto *scratch = static_cast<std::string *>(FlsGetValue(slot));
        if (!scratch)
        {
            scratch = new std::string();
            if (!FlsSetValue(slot, scratch))
            {
                delete scratch;
                throw std::bad_alloc();
            }
        }
        return *scratch;
    }
#endif

    /// @brief Owns short text inline and falls back to std::string only when needed.
    template <std::size_t InlineCapacity>
    class InlineLogText
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
            : id(source), name(std::move(displayName)), enabled(sourceEnabled)
        {
        }

        /// @brief Copies source metadata and atomically snapshots the enabled flag.
        /// @param other Source entry to copy.
        RegisteredSource(const RegisteredSource &other)
            : id(other.id), name(other.name), enabled(other.enabled.load(std::memory_order_relaxed))
        {
        }

        /// @brief Moves source metadata and atomically snapshots the enabled flag.
        /// @param other Source entry to move.
        RegisteredSource(RegisteredSource &&other) noexcept
            : id(other.id), name(std::move(other.name)), enabled(other.enabled.load(std::memory_order_relaxed))
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

    /// @brief Process-wide logger state.
    LoggerState loggerState;

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
            loggerState.activeProducers.fetch_add(1, std::memory_order_acq_rel);
            active = true;
            if ((loggerState.runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
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
            if (loggerState.activeProducers.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                loggerState.logCondition.notify_all();
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

    //-------------------------------------------------------------------------------------------------
    // Enum and result helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Converts a public level enum into its compact numeric representation.
    /// @param level Level to convert.
    /// @return Numeric level value used by atomics and bitmasks.
    std::uint8_t toLevelValue(LogLevel level)
    {
        return static_cast<std::uint8_t>(level);
    }

    /// @brief Converts a public output enum into its compact numeric representation.
    /// @param mode Output mode to convert.
    /// @return Numeric output mode value used by atomics.
    std::uint8_t toOutputModeValue(OutputMode mode)
    {
        return static_cast<std::uint8_t>(mode);
    }

    /// @brief Converts a public format policy into compact atomic storage.
    std::uint8_t toFormatPolicyValue(FormatPolicy policy)
    {
        switch (policy)
        {
        case FormatPolicy::StrictBounded:
            return 0;
        case FormatPolicy::FastNormal:
            return 1;
        }

        return 0;
    }

    /// @brief Converts compact atomic storage back to a public format policy.
    FormatPolicy formatPolicyFromValue(std::uint8_t value)
    {
        return value == 1 ? FormatPolicy::FastNormal : FormatPolicy::StrictBounded;
    }

    /// @brief Sanitizes invalid format policy enum values to the compatibility default.
    FormatPolicy sanitizeFormatPolicy(FormatPolicy policy)
    {
        switch (policy)
        {
        case FormatPolicy::StrictBounded:
        case FormatPolicy::FastNormal:
            return policy;
        }

        return FormatPolicy::StrictBounded;
    }

    /// @brief Checks whether a level value is one of the defined Logger::Types::Level values.
    /// @param level Level to validate.
    /// @return True when the value is in range.
    bool isValidLevel(LogLevel level)
    {
        return toLevelValue(level) < kLevelCount;
    }

    /// @brief Checks whether an output mode is one of the defined Logger::Types::Output values.
    /// @param mode Output mode to validate.
    /// @return True when the value is in range.
    bool isValidOutputMode(OutputMode mode)
    {
        switch (mode)
        {
        case OutputMode::None:
        case OutputMode::Console:
        case OutputMode::File:
        case OutputMode::Both:
            return true;
        }

        return false;
    }

    /// @brief Converts a valid level into a single-bit mask.
    /// @param level Level to convert.
    /// @return Bit for the level, or zero when the level is invalid.
    std::uint8_t levelBit(LogLevel level)
    {
        if (!isValidLevel(level))
        {
            return 0;
        }

        return static_cast<std::uint8_t>(1u << toLevelValue(level));
    }

    /// @brief Converts an atomic output-mode value back to the public enum.
    /// @param mode Numeric output mode value.
    /// @return Public Output value, clamped to None if the stored value is invalid.
    OutputMode outputModeFromValue(std::uint8_t mode)
    {
        switch (static_cast<OutputMode>(mode))
        {
        case OutputMode::None:
            return OutputMode::None;
        case OutputMode::Console:
            return OutputMode::Console;
        case OutputMode::File:
            return OutputMode::File;
        case OutputMode::Both:
            return OutputMode::Both;
        }

        return OutputMode::None;
    }

    /// @brief Packs hot-path logger state into one atomic word.
    /// @param running True when normal producer logs may be accepted.
    /// @param mode Current output mode.
    /// @param minLevel Startup minimum severity.
    /// @param levelMask Runtime enabled-level mask.
    /// @return Packed runtime-state word.
    std::uint32_t packRuntimeState(bool running, OutputMode mode, LogLevel minLevel, std::uint8_t levelMask)
    {
        std::uint32_t packed = running ? kRuntimeStateRunningBit : 0u;
        packed |= (static_cast<std::uint32_t>(toOutputModeValue(mode)) & kRuntimeStateEnumMask) << kRuntimeStateOutputShift;
        packed |= (static_cast<std::uint32_t>(toLevelValue(minLevel)) & kRuntimeStateEnumMask) << kRuntimeStateMinLevelShift;
        packed |= (static_cast<std::uint32_t>(levelMask) & kRuntimeStateLevelMaskMask) << kRuntimeStateLevelMaskShift;
        return packed;
    }

    /// @brief Extracts output mode from a packed runtime-state word.
    /// @param packed Packed runtime-state word.
    /// @return Current output mode.
    OutputMode runtimeStateOutput(std::uint32_t packed)
    {
        return outputModeFromValue(static_cast<std::uint8_t>((packed >> kRuntimeStateOutputShift) & kRuntimeStateEnumMask));
    }

    /// @brief Extracts min level from a packed runtime-state word.
    /// @param packed Packed runtime-state word.
    /// @return Packed min level converted to public enum.
    LogLevel runtimeStateMinLevel(std::uint32_t packed)
    {
        return static_cast<LogLevel>((packed >> kRuntimeStateMinLevelShift) & kRuntimeStateEnumMask);
    }

    /// @brief Extracts runtime level mask from a packed runtime-state word.
    /// @param packed Packed runtime-state word.
    /// @return Runtime enabled-level bitmask.
    std::uint8_t runtimeStateLevelMask(std::uint32_t packed)
    {
        return static_cast<std::uint8_t>((packed >> kRuntimeStateLevelMaskShift) & kRuntimeStateLevelMaskMask);
    }

    /// @brief Checks whether a platform error carries a real failure.
    /// @param error Platform error to inspect.
    /// @return True when the error source is not None.
    bool hasPlatformError(const PlatformError &error)
    {
        return error.source != PlatformErrorSource::None;
    }

    /// @brief Issues a cheap processor relax hint while waiting for a ring slot.
    void cpuRelax() noexcept
    {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
        _mm_pause();
#endif
    }

    /// @brief Waits for a claimed MPSC ring slot with bounded spinning before yielding.
    /// @param slot Slot claimed by ticket modulo capacity.
    /// @param ticket Expected sequence value for this producer.
    void waitForQueueSlot(QueueSlot &slot, std::size_t ticket)
    {
        std::uint32_t spins = 0;
        while (slot.sequence.load(std::memory_order_acquire) != ticket)
        {
            if (spins < kQueueSlotSpinBeforeYield)
            {
                ++spins;
                cpuRelax();
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    /// @brief Counts one queue-pressure drop in both lifetime and resettable diagnostics.
    /// @param counter Resettable queue-drop counter to increment.
    void recordQueueDropCounter(std::atomic<std::size_t> &counter)
    {
        loggerState.droppedLogs.fetch_add(1, std::memory_order_relaxed);
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Counts one diagnostic failure without affecting lifetime queue-drop reporting.
    /// @param counter Resettable diagnostic counter to increment.
    void recordDiagnosticFailureCounter(std::atomic<std::size_t> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Updates an atomic maximum value.
    /// @param target Atomic maximum to update.
    /// @param value Candidate value.
    void updateAtomicMax(std::atomic<std::size_t> &target, std::size_t value)
    {
        std::size_t current = target.load(std::memory_order_relaxed);
        while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

    /// @brief Reads resettable atomic counters into the public Stats type.
    /// @return Public stats snapshot.
    LoggerStats snapshotStats()
    {
        return LoggerStats{
            loggerState.stats.queued.load(std::memory_order_relaxed),
            loggerState.stats.written.load(std::memory_order_relaxed),
            loggerState.stats.queueDropsSoft.load(std::memory_order_relaxed),
            loggerState.stats.queueDropsHard.load(std::memory_order_relaxed),
            loggerState.stats.allocationFailures.load(std::memory_order_relaxed),
            loggerState.stats.fileWriteFailures.load(std::memory_order_relaxed),
            loggerState.stats.unknownSourceUses.load(std::memory_order_relaxed),
            loggerState.stats.formatFailures.load(std::memory_order_relaxed),
            loggerState.stats.truncated.load(std::memory_order_relaxed),
            loggerState.stats.peakQueueDepth.load(std::memory_order_relaxed)};
    }

    /// @brief Returns retained message arena bytes for the current queue storage.
    /// @pre loggerState.logMutex is held.
    /// @return Current ring and worker-batch message arena bytes.
    std::size_t messageArenaBytesUnlocked()
    {
        if (loggerState.inlineMessageCapacity == 0)
        {
            return 0;
        }

        std::size_t bytes = 0;
        if (loggerState.ringMessageArena)
        {
            bytes += loggerState.logRingSize * loggerState.inlineMessageCapacity;
        }
        if (loggerState.batchMessageArena)
        {
            bytes += loggerState.workerBatch.size() * loggerState.inlineMessageCapacity;
        }
        return bytes;
    }

    /// @brief Returns retained queue vector storage bytes for ring and worker batch entries.
    /// @pre loggerState.logMutex is held.
    /// @return Current queue vector capacity bytes.
    std::size_t queueStorageBytesUnlocked()
    {
        return (loggerState.logRingSize * sizeof(QueueSlot)) + (loggerState.workerBatch.capacity() * sizeof(QueuedLogEntry));
    }

    /// @brief Returns retained source registry storage bytes.
    /// @param registry Current source registry snapshot, or nullptr.
    /// @return Best-effort retained registry bytes.
    std::size_t sourceRegistryBytes(const SourceRegistry *registry)
    {
        if (!registry)
        {
            return 0;
        }

        std::size_t bytes = sizeof(SourceRegistry);
        bytes += registry->sources.capacity() * sizeof(RegisteredSource);
        bytes += registry->directSourceLookup.capacity() * sizeof(std::size_t);
        static const std::size_t emptyStringCapacity = std::string{}.capacity();
        for (const RegisteredSource &source : registry->sources)
        {
            const std::size_t nameCapacity = source.name.capacity();
            if (nameCapacity > emptyStringCapacity)
            {
                bytes += nameCapacity;
            }
        }
        return bytes;
    }

    /// @brief Returns bytes retained by the currently published shared source registry.
    /// @return Best-effort retained source registry bytes.
    std::size_t publishedSourceRegistryBytes()
    {
        const std::shared_ptr<SourceRegistry> registry = loggerState.sourceRegistry.load(std::memory_order_acquire);
        return sourceRegistryBytes(registry.get());
    }

    /// @brief Returns retained heap fallback capacity for one queued entry.
    /// @param entry Queue entry to inspect.
    /// @return Source and message heap fallback capacities.
    std::size_t entryTextHeapCapacityBytes(const QueuedLogEntry &entry)
    {
        return entry.sourceText.capacityBytes() + entry.message.capacityBytes();
    }

    /// @brief Checks whether queue-entry text capacity can be inspected without racing mutations.
    /// @pre loggerState.logMutex is held.
    /// @return True when producers are inactive, the queue is empty, and the worker is idle.
    bool entryTextHeapCapacityAvailableUnlocked()
    {
        return !loggerState.workerBusy &&
               loggerState.queueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState.publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState.activeProducers.load(std::memory_order_acquire) == 0;
    }

    /// @brief Returns retained heap fallback capacity in queue entries.
    /// @pre loggerState.logMutex is held.
    /// @note Worker batch entries are scanned only while the worker is idle to avoid racing worker mutation.
    /// @return Best-effort retained entry text heap capacity.
    std::size_t entryTextHeapCapacityBytesUnlocked()
    {
        if (!entryTextHeapCapacityAvailableUnlocked())
        {
            return 0;
        }

        std::size_t bytes = 0;
        for (std::size_t index = 0; index < loggerState.logRingSize; ++index)
        {
            bytes += entryTextHeapCapacityBytes(loggerState.logRing[index].entry);
        }

        if (!loggerState.workerBusy)
        {
            for (const QueuedLogEntry &entry : loggerState.workerBatch)
            {
                bytes += entryTextHeapCapacityBytes(entry);
            }
        }

        return bytes;
    }

    /// @brief Resets visible atomic stats counters.
    /// @param peakQueueDepth Peak queue depth to publish after reset.
    void resetAtomicStats(std::size_t peakQueueDepth = 0)
    {
        loggerState.stats.queued.store(0, std::memory_order_relaxed);
        loggerState.stats.written.store(0, std::memory_order_relaxed);
        loggerState.stats.queueDropsSoft.store(0, std::memory_order_relaxed);
        loggerState.stats.queueDropsHard.store(0, std::memory_order_relaxed);
        loggerState.stats.allocationFailures.store(0, std::memory_order_relaxed);
        loggerState.stats.fileWriteFailures.store(0, std::memory_order_relaxed);
        loggerState.stats.unknownSourceUses.store(0, std::memory_order_relaxed);
        loggerState.stats.formatFailures.store(0, std::memory_order_relaxed);
        loggerState.stats.truncated.store(0, std::memory_order_relaxed);
        loggerState.stats.peakQueueDepth.store(peakQueueDepth, std::memory_order_relaxed);
    }

    /// @brief Waits until producers that entered before shutdown/thread-start failure have left.
    void waitForActiveProducersToLeave()
    {
        while (loggerState.activeProducers.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
    }

    /// @brief Writes last-result state while logMutex is already held.
    /// @param result Result to publish.
    /// @param platformError Optional platform error to publish alongside result.
    void setResultUnlocked(LoggerResult result, PlatformError platformError = {})
    {
        loggerState.lastResult = result;
        loggerState.lastPlatformError = platformError;
    }

    /// @brief Writes last-result state from any thread.
    /// @param result Result to publish.
    /// @param platformError Optional platform error to publish alongside result.
    void recordResult(LoggerResult result, PlatformError platformError = {})
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        setResultUnlocked(result, platformError);
    }

    /// @brief Converts a structured platform failure into last-result state when needed.
    /// @param platformError Platform error returned by the platform bridge.
    void recordPlatformErrorIfAny(const PlatformError &platformError)
    {
        if (hasPlatformError(platformError))
        {
            recordResult(LoggerResult::PlatformCallFailed, platformError);
        }
    }

    /// @brief Preserves the first recoverable init warning while later setup keeps running.
    /// @param inOutResult Current init result.
    /// @param candidateResult New recoverable result to preserve if this is the first warning.
    /// @param inOutPlatformError Current platform error paired with inOutResult.
    /// @param candidatePlatformError Optional platform error paired with candidateResult.
    void preserveFirstInitResult(
        LoggerResult &inOutResult,
        LoggerResult candidateResult,
        PlatformError &inOutPlatformError,
        PlatformError candidatePlatformError = {})
    {
        if (inOutResult == LoggerResult::Success)
        {
            inOutResult = candidateResult;
            inOutPlatformError = candidatePlatformError;
        }
    }

    /// @brief Counts a file write/flush failure without stopping console/debug sinks.
    /// @param platformError Native failure details returned by the platform file bridge, when available.
    void recordFileWriteFailure(PlatformError platformError = {})
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.stats.fileWriteFailures.fetch_add(1, std::memory_order_relaxed);
        setResultUnlocked(LoggerResult::FileWriteFailed, platformError);
    }

    /// @brief Counts an allocation/internal-format failure with relaxed atomic accounting.
    void countAllocationFailure()
    {
        recordDiagnosticFailureCounter(loggerState.stats.allocationFailures);
    }

    /// @brief Counts a runtime format failure with relaxed atomic accounting.
    void countFormatFailure()
    {
        recordDiagnosticFailureCounter(loggerState.stats.formatFailures);
    }

    /// @brief Publishes locked configuration state into atomics used by hot paths.
    void publishRuntimeStateUnlocked()
    {
        loggerState.runtimeStateBits.store(
            packRuntimeState(loggerState.workerRunning, loggerState.mode, loggerState.minLevel, loggerState.enabledLevelMask),
            std::memory_order_release);
        loggerState.consoleColorEnabledAtomic.store(loggerState.consoleColorEnabled, std::memory_order_release);
        loggerState.debugOutputEnabledAtomic.store(loggerState.debugOutputEnabled, std::memory_order_release);
        loggerState.fatalPopupEnabledAtomic.store(loggerState.fatalPopupEnabled, std::memory_order_release);
        loggerState.flushConsoleEveryWriteAtomic.store(loggerState.flushConsoleEveryWrite, std::memory_order_release);
        loggerState.flushFileEveryBatchAtomic.store(loggerState.flushFileEveryBatch, std::memory_order_release);
    }

    /// @brief Publishes ANSI color support for stdout/stderr outside the hot write path.
    void publishConsoleColorSupport()
    {
        const bool allowColor = loggerState.consoleColorEnabled;
        loggerState.stdoutColorEnabledAtomic.store(
            allowColor && GameWIP::LoggerDetail::Platform::supportsAnsiColor(GameWIP::LoggerDetail::Platform::ConsoleStream::Stdout),
            std::memory_order_release);
        loggerState.stderrColorEnabledAtomic.store(
            allowColor && GameWIP::LoggerDetail::Platform::supportsAnsiColor(GameWIP::LoggerDetail::Platform::ConsoleStream::Stderr),
            std::memory_order_release);
    }

    /// @brief Returns cached ANSI-color availability for the selected console stream.
    bool consoleColorEnabledForStream(bool useCerr)
    {
        return useCerr
                   ? loggerState.stderrColorEnabledAtomic.load(std::memory_order_acquire)
                   : loggerState.stdoutColorEnabledAtomic.load(std::memory_order_acquire);
    }

    //-------------------------------------------------------------------------------------------------
    // Runtime filtering and source lookup
    //-------------------------------------------------------------------------------------------------

    /// @brief Loads the current shared source registry snapshot for lifecycle-safe lookup.
    /// @return Shared source registry snapshot, or nullptr when no sources are registered.
    std::shared_ptr<SourceRegistry> loadSourceRegistry()
    {
        return loggerState.sourceRegistry.load(std::memory_order_acquire);
    }

    /// @brief Finds a registered source by ID in a source registry snapshot.
    /// @param registry Source registry snapshot to search.
    /// @param source SourceId to find.
    /// @return Pointer to source metadata, or nullptr when the ID is unknown.
    RegisteredSource *findSource(SourceRegistry &registry, SourceId source)
    {
        if (!registry.directSourceLookup.empty() && source >= registry.directSourceBase)
        {
            const SourceId offset = source - registry.directSourceBase;
            if (offset < registry.directSourceLookup.size())
            {
                const std::size_t index = registry.directSourceLookup[offset];
                if (index != kInvalidSourceIndex)
                {
                    return &registry.sources[index];
                }
            }
        }

        const auto position = std::lower_bound(
            registry.sources.begin(),
            registry.sources.end(),
            source,
            [](const RegisteredSource &candidate, SourceId id)
            {
                return candidate.id < id;
            });

        if (position != registry.sources.end() && position->id == source)
        {
            return &(*position);
        }

        return nullptr;
    }

    /// @brief Finds a registered source by ID in a source registry snapshot.
    /// @param registry Source registry snapshot to search.
    /// @param source SourceId to find.
    /// @return Pointer to source metadata, or nullptr when the ID is unknown.
    const RegisteredSource *findSource(const SourceRegistry &registry, SourceId source)
    {
        if (!registry.directSourceLookup.empty() && source >= registry.directSourceBase)
        {
            const SourceId offset = source - registry.directSourceBase;
            if (offset < registry.directSourceLookup.size())
            {
                const std::size_t index = registry.directSourceLookup[offset];
                if (index != kInvalidSourceIndex)
                {
                    return &registry.sources[index];
                }
            }
        }

        const auto position = std::lower_bound(
            registry.sources.begin(),
            registry.sources.end(),
            source,
            [](const RegisteredSource &candidate, SourceId id)
            {
                return candidate.id < id;
            });

        if (position != registry.sources.end() && position->id == source)
        {
            return &(*position);
        }

        return nullptr;
    }

    /// @brief Rebuilds compact direct source lookup when registered IDs are dense enough.
    /// @param registry Source registry with a sorted source table.
    void rebuildSourceLookup(SourceRegistry &registry)
    {
        registry.directSourceLookup.clear();
        registry.directSourceBase = 0;
        if (registry.sources.empty())
        {
            return;
        }

        const SourceId minSource = registry.sources.front().id;
        const SourceId maxSource = registry.sources.back().id;
        const std::uint64_t span64 = static_cast<std::uint64_t>(maxSource) - static_cast<std::uint64_t>(minSource) + 1u;
        const std::uint64_t densityLimit = std::max<std::uint64_t>(64u, static_cast<std::uint64_t>(registry.sources.size()) * 4u);
        if (span64 > densityLimit || span64 > 4096u)
        {
            return;
        }

        registry.directSourceBase = minSource;
        registry.directSourceLookup.assign(static_cast<std::size_t>(span64), kInvalidSourceIndex);
        for (std::size_t index = 0; index < registry.sources.size(); ++index)
        {
            registry.directSourceLookup[registry.sources[index].id - minSource] = index;
        }
    }

    bool sourceEnabledRuntime(const SourceRegistry *registry, SourceId source);

    /// @brief Checks the runtime source filter.
    /// @param source SourceId to test.
    /// @return True when the source is unknown or registered and enabled.
    /// @note Unknown sources intentionally log as UnknownSource instead of dropping.
    bool sourceEnabledRuntime(SourceId source)
    {
        const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
        return sourceEnabledRuntime(registry.get(), source);
    }

    /// @brief Checks the runtime source filter using a caller-owned registry snapshot.
    /// @param registry Source registry snapshot, or nullptr when no sources are registered.
    /// @param source SourceId to test.
    /// @return True when the source is unknown or registered and enabled.
    bool sourceEnabledRuntime(const SourceRegistry *registry, SourceId source)
    {
        if (!registry)
        {
            return true;
        }

        const RegisteredSource *registeredSource = findSource(*registry, source);
        return registeredSource == nullptr || registeredSource->enabled.load(std::memory_order_acquire);
    }

    /// @brief Hot-path severity-only log gate used before formatting/allocation.
    /// @param level Level to test.
    /// @return True when the logger is running, output is enabled, minLevel passes, and the level is enabled.
    bool shouldLogRuntime(LogLevel level)
    {
        if (!isValidLevel(level))
        {
            return false;
        }

        const std::uint32_t runtimeState = loggerState.runtimeStateBits.load(std::memory_order_acquire);
        if ((runtimeState & kRuntimeStateRunningBit) == 0)
        {
            return false;
        }

        if (runtimeStateOutput(runtimeState) == OutputMode::None)
        {
            return false;
        }

        if (toLevelValue(level) < toLevelValue(runtimeStateMinLevel(runtimeState)))
        {
            return false;
        }

        const std::uint8_t bit = levelBit(level);
        return bit != 0 && (runtimeStateLevelMask(runtimeState) & bit) != 0;
    }

    /// @brief Hot-path registered source log gate used before formatting/allocation.
    /// @param level Level to test.
    /// @param source SourceId to test.
    /// @return True when shouldLogRuntime(level) passes and the source filter allows the source.
    bool shouldLogRuntime(LogLevel level, SourceId source)
    {
        return shouldLogRuntime(level) && sourceEnabledRuntime(source);
    }

    /// @brief Rechecks a pending entry against the current packed runtime state.
    /// @param entry Entry to test.
    /// @return Accept/drop reason for the enqueue path.
    /// @note This second check closes the race where filters change after formatting but before enqueue.
    FilterDecision checkPendingEntryAcceptedUnlocked(const PendingLogEntry &entry)
    {
        const std::uint32_t runtimeState = loggerState.runtimeStateBits.load(std::memory_order_acquire);
        if (!isValidLevel(entry.level) || (runtimeState & kRuntimeStateRunningBit) == 0 || runtimeStateOutput(runtimeState) == OutputMode::None)
        {
            return {};
        }

        if (entry.bypassFilters)
        {
            return {true};
        }

        if (toLevelValue(entry.level) < toLevelValue(runtimeStateMinLevel(runtimeState)))
        {
            return {};
        }

        const std::uint8_t levelMaskBit = levelBit(entry.level);
        if (levelMaskBit == 0 || (runtimeStateLevelMask(runtimeState) & levelMaskBit) == 0)
        {
            return {};
        }

        if (entry.usesRegisteredSource && !sourceEnabledRuntime(entry.sourceId))
        {
            return {};
        }

        return {true};
    }

    //-------------------------------------------------------------------------------------------------
    // Output style and sink selection helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Returns output label/color/stream routing for a level.
    /// @param level Level to style.
    /// @return Style data for the requested level.
    LogStyle getLogStyle(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return {"TRACE", "\033[90m", false};
        case LogLevel::Debug:
            return {"DEBUG", "\033[36m", false};
        case LogLevel::Info:
            return {"INFO", "", false};
        case LogLevel::Warn:
            return {"WARN", "\033[33m", false};
        case LogLevel::Error:
            return {"ERROR", "\033[31m", true};
        case LogLevel::Fatal:
            return {"FATAL", "\033[31m", true};
        }

        return {};
    }

    /// @brief Checks whether a level is allowed to drop at the soft queue limit.
    /// @param level Level to test.
    /// @return True for Trace, Debug, Info, and Warn.
    bool isLowPriority(LogLevel level)
    {
        return level <= LogLevel::Warn;
    }

    /// @brief Checks whether an output mode includes console output.
    /// @param mode Output mode to inspect.
    /// @return True when stdout/stderr should receive normal logs.
    bool hasConsoleOutput(OutputMode mode)
    {
        return mode == OutputMode::Console || mode == OutputMode::Both;
    }

    /// @brief Checks whether an output mode includes file output.
    /// @param mode Output mode to inspect.
    /// @return True when the file sink should receive normal logs.
    bool hasFileOutput(OutputMode mode)
    {
        return mode == OutputMode::File || mode == OutputMode::Both;
    }

    //-------------------------------------------------------------------------------------------------
    // Init validation and storage helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Computes ceil(softQueueSize * multiplier) while avoiding size_t overflow.
    /// @param softQueueSize Configured soft queue limit.
    /// @param multiplier Hard queue capacity multiplier.
    /// @return Hard queue limit.
    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier)
    {
        const long double preciseLimit = static_cast<long double>(softQueueSize) * static_cast<long double>(multiplier);
        const long double maxLimit = static_cast<long double>(std::numeric_limits<std::size_t>::max());
        if (preciseLimit >= maxLimit)
        {
            return std::numeric_limits<std::size_t>::max();
        }

        const std::size_t hardLimit = static_cast<std::size_t>(std::ceil(preciseLimit));
        return std::max(softQueueSize, hardLimit);
    }

    /// @brief Sanitizes the hard queue multiplier.
    /// @param inOutHardQueueMultiplier Configured multiplier, overwritten with the sanitized multiplier.
    /// @param softQueueSize Sanitized soft queue limit.
    /// @param inOutResult Receives InvalidQueueSize if the requested multiplier is invalid.
    /// @return Effective hard queue limit.
    std::size_t effectiveHardQueueLimit(double &inOutHardQueueMultiplier, std::size_t softQueueSize, LoggerResult &inOutResult)
    {
        if (!std::isfinite(inOutHardQueueMultiplier) || inOutHardQueueMultiplier < 1.0)
        {
            if (inOutResult == LoggerResult::Success)
            {
                inOutResult = LoggerResult::InvalidQueueSize;
            }
            inOutHardQueueMultiplier = 1.0;
            return softQueueSize;
        }

        return computeHardQueueLimit(softQueueSize, inOutHardQueueMultiplier);
    }

    /// @brief Sanitizes and clamps worker batch size against the hard queue limit.
    /// @param requested Configured worker batch size, where zero means default.
    /// @param hardLimit Effective hard queue limit.
    /// @return Effective worker batch size in [1, hardLimit].
    std::size_t effectiveWorkerBatchSize(std::size_t requested, std::size_t hardLimit)
    {
        const std::size_t wanted = requested == 0 ? kDefaultWorkerBatchSize : requested;
        return std::clamp(wanted, std::size_t{1}, hardLimit);
    }

    /// @brief Allocates uninitialized message arena bytes for a queue container.
    /// @param entryCount Number of queue entries backed by the arena.
    /// @param inlineMessageCapacity Bytes reserved per entry.
    /// @param arena Output arena owner.
    /// @return True when allocation succeeded or no arena is needed.
    bool allocateMessageArena(std::size_t entryCount, std::size_t inlineMessageCapacity, std::unique_ptr<char[]> &arena)
    {
        arena.reset();
        if (entryCount == 0 || inlineMessageCapacity == 0)
        {
            return true;
        }

        if (entryCount > std::numeric_limits<std::size_t>::max() / inlineMessageCapacity)
        {
            return false;
        }

        arena.reset(new char[entryCount * inlineMessageCapacity]);
        return true;
    }

    /// @brief Allocates ring and worker batch storage before init commits state.
    /// @param hardLimit Ring buffer capacity to allocate.
    /// @param workerBatchSize Worker batch capacity to allocate.
    /// @param inlineMessageCapacity Per-entry message arena bytes.
    /// @param ring Output ring storage.
    /// @param ringSize Receives the number of slots in ring.
    /// @param batch Output reusable worker batch storage.
    /// @param ringArena Output ring message arena.
    /// @param batchArena Output worker batch message arena.
    /// @return True when both containers were allocated successfully.
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

    /// @brief Copies, sorts, validates, and initially filters source definitions.
    /// @param definitions Source definitions supplied by Config.
    /// @param filters Initial source filters supplied by Config.
    /// @param registry Output source registry snapshot.
    /// @param outResult Receives the specific validation error on failure.
    /// @return True when all source definitions and filters are valid.
    bool prepareSources(
        std::span<const SourceDefinition> definitions,
        std::span<const SourceFilter> filters,
        std::shared_ptr<SourceRegistry> &registry,
        LoggerResult &outResult)
    {
        try
        {
            registry.reset();
            if (definitions.empty() && filters.empty())
            {
                return true;
            }

            std::shared_ptr<SourceRegistry> preparedRegistry = std::make_shared<SourceRegistry>();
            std::vector<RegisteredSource> &sources = preparedRegistry->sources;
            sources.reserve(definitions.size());
            for (const SourceDefinition &definition : definitions)
            {
                if (definition.name.empty())
                {
                    outResult = LoggerResult::InvalidSourceDefinition;
                    return false;
                }

                sources.emplace_back(definition.id, std::string(definition.name), true);
            }

            std::sort(sources.begin(), sources.end(), [](const RegisteredSource &left, const RegisteredSource &right)
                      { return left.id < right.id; });

            const auto duplicateSource = std::adjacent_find(sources.begin(), sources.end(), [](const RegisteredSource &left, const RegisteredSource &right)
                                                            { return left.id == right.id; });
            if (duplicateSource != sources.end())
            {
                outResult = LoggerResult::InvalidSourceDefinition;
                return false;
            }

            std::vector<SourceId> filteredSources;
            filteredSources.reserve(filters.size());
            for (const SourceFilter &filter : filters)
            {
                auto sourcePosition = std::lower_bound(
                    sources.begin(),
                    sources.end(),
                    filter.source,
                    [](const RegisteredSource &candidate, SourceId id)
                    {
                        return candidate.id < id;
                    });

                if (sourcePosition == sources.end() || sourcePosition->id != filter.source)
                {
                    outResult = LoggerResult::InvalidSourceFilter;
                    return false;
                }

                filteredSources.push_back(filter.source);
                sourcePosition->enabled.store(filter.enabled, std::memory_order_relaxed);
            }

            std::sort(filteredSources.begin(), filteredSources.end());
            const auto duplicateFilter = std::adjacent_find(filteredSources.begin(), filteredSources.end());
            if (duplicateFilter != filteredSources.end())
            {
                outResult = LoggerResult::InvalidSourceFilter;
                return false;
            }

            rebuildSourceLookup(*preparedRegistry);
            registry = std::move(preparedRegistry);
            return true;
        }
        catch (...)
        {
            registry.reset();
            outResult = LoggerResult::InvalidSourceDefinition;
            return false;
        }
    }

    /// @brief Builds the initial runtime level-filter bitmask from Config.
    /// @param filters Initial level filters supplied by Config.
    /// @param outMask Receives the resulting enabled-level bitmask.
    /// @return True when all level filters are valid and non-duplicated.
    bool prepareLevelMask(std::span<const LevelFilter> filters, std::uint8_t &outMask)
    {
        outMask = kAllLevelMask;
        std::uint8_t seenLevels = 0;
        for (const LevelFilter &filter : filters)
        {
            const std::uint8_t bit = levelBit(filter.level);
            if (bit == 0 || (seenLevels & bit) != 0)
            {
                return false;
            }

            seenLevels |= bit;
            if (filter.enabled)
            {
                outMask = static_cast<std::uint8_t>(outMask | bit);
            }
            else
            {
                outMask = static_cast<std::uint8_t>(outMask & ~bit);
            }
        }

        return true;
    }

    //-------------------------------------------------------------------------------------------------
    // Queue entry storage helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Clears queued entries while preserving allocated queue and batch capacity.
    /// @pre loggerState.logMutex is held.
    void clearQueueUnlocked()
    {
        const bool releaseHeapCapacity = loggerState.releaseMessageMemoryAfterWrite;
        for (std::size_t index = 0; index < loggerState.logRingSize; ++index)
        {
            QueueSlot &slot = loggerState.logRing[index];
            QueuedLogEntry &entry = slot.entry;
            entry.bypassFilters = false;
            entry.usesRegisteredSource = false;
            entry.sourceId = 0;
            entry.sourceText.clear(releaseHeapCapacity);
            entry.message.clear(releaseHeapCapacity);
            slot.skip = false;
            slot.sequence.store(index, std::memory_order_relaxed);
        }

        for (QueuedLogEntry &entry : loggerState.workerBatch)
        {
            entry.bypassFilters = false;
            entry.usesRegisteredSource = false;
            entry.sourceId = 0;
            entry.sourceText.clear(releaseHeapCapacity);
            entry.message.clear(releaseHeapCapacity);
        }
        loggerState.enqueueTicket.store(0, std::memory_order_relaxed);
        loggerState.dequeueTicket.store(0, std::memory_order_relaxed);
        loggerState.queueDepth.store(0, std::memory_order_relaxed);
        loggerState.publishedQueueDepth.store(0, std::memory_order_relaxed);
    }

    /// @brief Releases retained queue, batch, arena, and source-registry storage.
    /// @pre loggerState.logMutex is held and no worker/producers are mutating queue storage.
    void releaseRuntimeStorageUnlocked()
    {
        loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        loggerState.ringMessageArena.reset();
        loggerState.batchMessageArena.reset();
        loggerState.logRing.reset();
        loggerState.logRingSize = 0;
        std::vector<QueuedLogEntry>{}.swap(loggerState.workerBatch);
        loggerState.softQueueSize = 0;
        loggerState.hardQueueSize = 0;
        loggerState.workerBatchSize = 0;
        loggerState.inlineMessageCapacity = 0;
    }

    /// @brief Clears one queued entry while keeping configured message storage.
    /// @param entry Entry to clear.
    void clearLogEntry(QueuedLogEntry &entry)
    {
        const bool releaseHeapCapacity = loggerState.releaseMessageMemoryAfterWrite;
        entry.level = LogLevel::Info;
        entry.bypassFilters = false;
        entry.usesRegisteredSource = false;
        entry.sourceId = 0;
        entry.sourceText.clear(releaseHeapCapacity);
        entry.message.clear(releaseHeapCapacity);
    }

    /// @brief Copies only the retained message prefix and truncation suffix into queue storage.
    /// @details This keeps preformatted huge messages from being fully copied before truncation.
    /// @param entry Queue entry that will own the retained message text.
    /// @param message Message text supplied by the producer.
    /// @param maxMessageLength Maximum stored bytes.
    /// @param outTruncated Receives true when the message was truncated.
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

        std::string retainedMessage;
        retainedMessage.reserve(maxMessageLength);
        retainedMessage.append(message.substr(0, maxMessageLength - suffix.size()));
        retainedMessage.append(suffix);
        entry.message.assign(retainedMessage);
    }

    /// @brief Copies a pending producer entry into an owning queue slot.
    /// @param destination Ring or batch slot to mutate.
    /// @param source Pending entry to copy.
    /// @param outTruncated Receives true when the message was truncated.
    void copyPendingEntryToQueueSlot(QueuedLogEntry &destination, const PendingLogEntry &source, bool &outTruncated)
    {
        destination.level = source.level;
        destination.bypassFilters = source.bypassFilters;
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
            assignRetainedMessage(destination, source.message, loggerState.maxMessageLength, outTruncated);
        }
    }

    /// @brief Transfers an owning queued entry into another slot without copying heap message storage.
    /// @param destination Destination slot.
    /// @param source Source slot.
    void moveQueuedEntry(QueuedLogEntry &destination, QueuedLogEntry &source)
    {
        destination.level = source.level;
        destination.bypassFilters = source.bypassFilters;
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
        std::size_t depth = loggerState.queueDepth.load(std::memory_order_acquire);
        while (true)
        {
            if (depth >= loggerState.hardQueueSize)
            {
                return EnqueueStatus::DroppedHard;
            }
            if (depth >= loggerState.softQueueSize && isLowPriority(entry.level))
            {
                return EnqueueStatus::DroppedSoft;
            }
            if (loggerState.queueDepth.compare_exchange_weak(
                    depth,
                    depth + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                outPreviousDepth = depth;
                return EnqueueStatus::Queued;
            }
        }
    }

    /// @brief Publishes a filled or skip-marked slot and wakes the worker when it was sleeping.
    /// @param slot Slot to publish.
    /// @param ticket Producer ticket for this slot.
    /// @param outNotifyWorker Receives true when the published-depth transition should wake the worker.
    void publishQueueSlot(QueueSlot &slot, std::size_t ticket, bool &outNotifyWorker)
    {
        slot.sequence.store(ticket + 1, std::memory_order_release);
        outNotifyWorker = loggerState.publishedQueueDepth.fetch_add(1, std::memory_order_acq_rel) == 0;
    }

    /// @brief Publishes one pending entry into a reserved MPSC ring slot.
    /// @param entry Pending entry to enqueue.
    /// @param outTruncated Receives true when stored message text was truncated.
    /// @param outNotifyWorker Receives true when the worker should be woken for newly published work.
    /// @return Queued on success, or AllocationFailure after publishing a skip marker.
    EnqueueStatus publishReservedQueueEntry(const PendingLogEntry &entry, bool &outTruncated, bool &outNotifyWorker)
    {
        outNotifyWorker = false;
        const std::size_t capacity = loggerState.logRingSize;
        if (capacity == 0)
        {
            loggerState.queueDepth.fetch_sub(1, std::memory_order_acq_rel);
            return EnqueueStatus::DroppedHard;
        }

        const std::size_t ticket = loggerState.enqueueTicket.fetch_add(1, std::memory_order_acq_rel);
        QueueSlot &slot = loggerState.logRing[ticket % capacity];
        waitForQueueSlot(slot, ticket);

        try
        {
#if GAMEWIP_LOGGER_TEST_HOOKS
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
        const std::size_t capacity = loggerState.logRingSize;
        const std::size_t batchLimit = batch.empty() ? capacity : batch.size();
        if (capacity == 0 || batchLimit == 0)
        {
            return 0;
        }

        while (batchCount < batchLimit)
        {
            const std::size_t ticket = loggerState.dequeueTicket.load(std::memory_order_relaxed);
            QueueSlot &slot = loggerState.logRing[ticket % capacity];
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
            loggerState.dequeueTicket.store(ticket + 1, std::memory_order_release);
            loggerState.publishedQueueDepth.fetch_sub(1, std::memory_order_acq_rel);
            loggerState.queueDepth.fetch_sub(1, std::memory_order_acq_rel);
        }

        return batchCount;
    }

    //-------------------------------------------------------------------------------------------------
    // Formatting and sink helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Formats local time through the platform bridge and returns a fallback on failure.
    /// @param time Time value to format.
    /// @param timeFormat strftime-compatible format string.
    /// @param outError Receives platform error details.
    /// @return Formatted time text, or "invalid-time" on platform failure.
    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, PlatformError &outError)
    {
        std::string text;
        outError = GameWIP::LoggerDetail::Platform::formatLocalTime(time, timeFormat, text);
        if (hasPlatformError(outError))
        {
            return "invalid-time";
        }

        return text;
    }

    /// @brief Formats the current local time.
    /// @param timeFormat strftime-compatible format string.
    /// @param outError Receives platform error details.
    /// @return Formatted current local time, or "invalid-time" on platform failure.
    std::string getCurrentTimeText(std::string_view timeFormat, PlatformError &outError)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        return formatTimeOrFallback(time, timeFormat, outError);
    }

    /// @brief Returns a cached per-second timestamp, refreshing the cache when needed.
    /// @param cache Timestamp cache to read/update.
    /// @return View into cache.text.
    std::string_view getTimestampText(TimestampCache &cache)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentSecond = std::chrono::system_clock::to_time_t(now);

        if (!cache.valid || cache.second != currentSecond)
        {
            PlatformError platformError;
            cache.text = formatTimeOrFallback(currentSecond, "%H:%M:%S", platformError);
            if (hasPlatformError(platformError))
            {
                recordResult(LoggerResult::PlatformCallFailed, platformError);
            }

            cache.second = currentSecond;
            cache.valid = true;
        }

        return cache.text;
    }

    /// @brief Returns a copy of the platform debug output timestamp cache text.
    /// @return Timestamp string safe to use after releasing debugTimestampMutex.
    std::string getDebugTimestampText()
    {
        std::lock_guard<std::mutex> lock(loggerState.debugTimestampMutex);
        return std::string(getTimestampText(loggerState.debugTimestampCache));
    }

    /// @brief Resolves a SourceId into display text.
    /// @param source SourceId to resolve.
    /// @param outUnknownSource Receives true when the source was not registered.
    /// @return Registered source name, or "UnknownSource".
    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource)
    {
        outUnknownSource = false;
        if (registry)
        {
            const RegisteredSource *registeredSource = findSource(*registry, source);
            if (registeredSource != nullptr)
            {
                return registeredSource->name;
            }
        }

        outUnknownSource = true;
        return "UnknownSource";
    }

    /// @brief Resolves the source text for a queued entry.
    /// @param entry Queued entry to inspect.
    /// @param outUnknownSource Receives true when a registered source entry used an unknown SourceId.
    /// @return Source text to write in the log line.
    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource)
    {
        if (!entry.usesRegisteredSource)
        {
            outUnknownSource = false;
            return entry.sourceText.view();
        }

        return findSourceName(registry, entry.sourceId, outUnknownSource);
    }

    /// @brief Increments the unknown source counter after a message is preserved and written.
    void recordUnknownSourceUse()
    {
        loggerState.stats.unknownSourceUses.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Stores message text in an entry, truncating with a suffix when needed.
    /// @param entry Entry to mutate.
    /// @param message Message text to copy.
    /// @param maxMessageLength Maximum stored message length.
    /// @param outTruncated Receives true when truncation occurred.
    void assignMessage(QueuedLogEntry &entry, std::string_view message, std::size_t maxMessageLength, bool &outTruncated)
    {
        outTruncated = message.size() > maxMessageLength;
        if (!outTruncated)
        {
            entry.message.assign(message);
            return;
        }

        constexpr std::string_view suffix = "... [truncated]";
        if (maxMessageLength <= suffix.size())
        {
            entry.message.assign(suffix.substr(0, maxMessageLength));
            return;
        }

        entry.message.assignJoined(message.substr(0, maxMessageLength - suffix.size()), suffix);
    }

    /// @brief Builds a truncated message copy using the same suffix as queued entries.
    /// @param outMessage Receives the truncated text.
    /// @param message Source message text.
    /// @param maxMessageLength Maximum stored/debugged message length.
    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength)
    {
        constexpr std::string_view suffix = "... [truncated]";
        outMessage.clear();
        if (maxMessageLength == 0)
        {
            return;
        }
        if (maxMessageLength <= suffix.size())
        {
            outMessage.assign(suffix.substr(0, maxMessageLength));
            return;
        }

        outMessage.reserve(maxMessageLength);
        outMessage.append(message.substr(0, maxMessageLength - suffix.size()));
        outMessage.append(suffix);
    }

    /// @brief Returns a message view bounded by the active maximum message length.
    /// @param message Original message.
    /// @param alreadyTruncated True when the caller already applied Logger truncation.
    /// @param scratch Scratch storage used only when truncation is needed.
    /// @param outTruncated Receives true when this call truncated message.
    /// @return Either message or a view into scratch.
    std::string_view boundedMessageView(std::string_view message, bool alreadyTruncated, std::string &scratch, bool &outTruncated)
    {
        outTruncated = false;
        if (alreadyTruncated)
        {
            return message;
        }

        const std::size_t maxMessageLength = loggerState.maxMessageLengthAtomic.load(std::memory_order_acquire);
        if (message.size() <= maxMessageLength)
        {
            return message;
        }

        buildTruncatedMessage(scratch, message, maxMessageLength);
        outTruncated = true;
        return scratch;
    }

    /// @brief Builds the final [time][level][source]: message line without std::format.
    /// @param outMessage Reusable output scratch string.
    /// @param timestamp Timestamp text.
    /// @param levelText Level label.
    /// @param source Source text.
    /// @param message Message text.
    void buildLogLine(std::string &outMessage, std::string_view timestamp, std::string_view levelText, std::string_view source, std::string_view message)
    {
        constexpr std::size_t fixedFormatLength = 8;
        const std::size_t requiredCapacity = timestamp.size() + levelText.size() + source.size() + message.size() + fixedFormatLength;

        outMessage.clear();
        if (outMessage.capacity() < requiredCapacity)
        {
            outMessage.reserve(requiredCapacity);
        }

        outMessage.append("[");
        outMessage.append(timestamp);
        outMessage.append("][");
        outMessage.append(levelText);
        outMessage.append("][");
        outMessage.append(source);
        outMessage.append("]: ");
        outMessage.append(message);
    }

    PlatformError openFileExclusiveForLogger(std::string_view path, FileHandle &outHandle);
    PlatformError writeFileForLogger(FileHandle handle, std::string_view text);
    PlatformError flushFileForLogger(FileHandle handle);

    /// @brief Writes one report directly to configured sinks without using the async queue.
    /// @param level Severity for the report line.
    /// @param source Source text to write.
    /// @param message Message text to write, truncated to the active message limit if needed.
    /// @param unknownSource True when source came from an unregistered SourceId.
    /// @param alreadyTruncated True when the caller already bounded the message and appended the truncation suffix.
    /// @return True when at least one configured normal sink accepted the line.
    bool writeReportSynchronously(LogLevel level, std::string_view source, std::string_view message, bool unknownSource = false, bool alreadyTruncated = false)
    {
        try
        {
            const std::uint32_t runtimeState = loggerState.runtimeStateBits.load(std::memory_order_acquire);
            const OutputMode mode = runtimeStateOutput(runtimeState);
            if (mode == OutputMode::None || !isValidLevel(level))
            {
                return false;
            }

            const bool consoleOutput = hasConsoleOutput(mode);
            const bool wantsFileOutput = hasFileOutput(mode);
            if (!consoleOutput && !wantsFileOutput)
            {
                return false;
            }

            std::size_t maxMessageLength = 0;
            {
                std::lock_guard<std::mutex> lock(loggerState.logMutex);
                maxMessageLength = loggerState.maxMessageLength;
            }

            constexpr std::string_view suffix = "... [truncated]";
            std::string truncatedMessage;
            std::string_view messageText = message;
            const bool needsTruncation = message.size() > maxMessageLength;
            const bool truncated = alreadyTruncated || needsTruncation;
            if (needsTruncation)
            {
                if (maxMessageLength <= suffix.size())
                {
                    truncatedMessage.assign(suffix.substr(0, maxMessageLength));
                }
                else
                {
                    truncatedMessage.reserve(maxMessageLength);
                    truncatedMessage.append(message.substr(0, maxMessageLength - suffix.size()));
                    truncatedMessage.append(suffix);
                }
                messageText = truncatedMessage;
            }

            TimestampCache timestampCache;
            std::string line;
            const LogStyle style = getLogStyle(level);
            buildLogLine(line, getTimestampText(timestampCache), style.text, source, messageText);

            bool accepted = false;
            bool fileWriteFailed = false;
            PlatformError fileErrorDetail;
            {
                std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
                if (consoleOutput)
                {
                    std::ostream &consoleStream = style.useCerr ? std::cerr : std::cout;
                    if (consoleColorEnabledForStream(style.useCerr) && style.color[0] != '\0')
                    {
                        consoleStream << style.color << line << "\033[0m";
                    }
                    else
                    {
                        consoleStream << line;
                    }

                    consoleStream << '\n';
                    if (loggerState.flushConsoleEveryWriteAtomic.load(std::memory_order_acquire))
                    {
                        consoleStream.flush();
                    }
                    accepted = accepted || !consoleStream.fail();
                }

                if (wantsFileOutput)
                {
                    if (loggerState.fileOutputAvailableAtomic.load(std::memory_order_acquire) && GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
                    {
                        std::string fileLine(line);
                        fileLine.push_back('\n');
                        fileErrorDetail = writeFileForLogger(loggerState.logFile, fileLine);
                        fileWriteFailed = hasPlatformError(fileErrorDetail);
                        accepted = accepted || !fileWriteFailed;
                    }
                    else
                    {
                        fileErrorDetail = PlatformError{PlatformErrorSource::File, 0};
                        fileWriteFailed = true;
                    }
                }
            }

            if (fileWriteFailed)
            {
                recordFileWriteFailure(fileErrorDetail);
            }

            if (accepted)
            {
                loggerState.stats.written.fetch_add(1, std::memory_order_relaxed);
                if (truncated)
                {
                    loggerState.stats.truncated.fetch_add(1, std::memory_order_relaxed);
                }
                if (unknownSource)
                {
                    recordUnknownSourceUse();
                }
            }

            return accepted;
        }
        catch (...)
        {
            return false;
        }
    }

#if GAMEWIP_LOGGER_TEST_HOOKS
    /// @brief Returns a synthetic platform file error used by test hooks.
    PlatformError forcedFileError() noexcept
    {
        return PlatformError{PlatformErrorSource::File, 1};
    }

    /// @brief Returns a synthetic fatal-popup error used by test hooks.
    PlatformError forcedFatalPopupError() noexcept
    {
        return PlatformError{PlatformErrorSource::FatalPopup, 1};
    }
#endif

    /// @brief Opens a file, optionally consuming a test hook that forces failure.
    PlatformError openFileExclusiveForLogger(std::string_view path, FileHandle &outHandle)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileOpenFailure))
        {
            outHandle = {};
            return forcedFileError();
        }
#endif
        return GameWIP::LoggerDetail::Platform::openFileExclusive(path, outHandle);
    }

    /// @brief Writes file text, optionally consuming a test hook that forces failure.
    PlatformError writeFileForLogger(FileHandle handle, std::string_view text)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileWriteFailure))
        {
            return forcedFileError();
        }
#endif
        return GameWIP::LoggerDetail::Platform::writeFile(handle, text);
    }

    /// @brief Flushes a file, optionally consuming a test hook that forces failure.
    PlatformError flushFileForLogger(FileHandle handle)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileFlushFailure))
        {
            return forcedFileError();
        }
#endif
        return GameWIP::LoggerDetail::Platform::flushFile(handle);
    }

    /// @brief Resolves a SourceId and writes one report directly to configured sinks.
    /// @param level Severity for the report line.
    /// @param source SourceId to resolve.
    /// @param message Message text to write.
    /// @param alreadyTruncated True when the caller already bounded the message and appended the truncation suffix.
    /// @return True when at least one configured normal sink accepted the line.
    bool writeReportSynchronously(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated = false)
    {
        const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
        bool unknownSource = false;
        const std::string_view sourceText = findSourceName(registry.get(), source, unknownSource);
        return writeReportSynchronously(level, sourceText, message, unknownSource, alreadyTruncated);
    }

    /// @brief Writes one entry to console immediately and appends file text to the batch buffer.
    /// @param entry Entry to write.
    /// @param timestampCache Worker timestamp cache.
    /// @param lineScratch Reusable line scratch buffer.
    /// @param fileBatchScratch Reusable file batch buffer.
    /// @return Sink acceptance details for stats accounting.
    /// @note Runtime filters are rechecked here so queued entries can still be suppressed after a filter change.
    SinkWriteResult writeLogEntry(const QueuedLogEntry &entry, TimestampCache &timestampCache, std::string &lineScratch, std::string &fileBatchScratch)
    {
        SinkWriteResult result;
        const std::uint32_t runtimeState = loggerState.runtimeStateBits.load(std::memory_order_acquire);
        const OutputMode mode = runtimeStateOutput(runtimeState);
        if (mode == OutputMode::None || !isValidLevel(entry.level))
        {
            return result;
        }

        if (!entry.bypassFilters && toLevelValue(entry.level) < toLevelValue(runtimeStateMinLevel(runtimeState)))
        {
            return result;
        }

        const std::shared_ptr<SourceRegistry> registry = entry.usesRegisteredSource ? loadSourceRegistry() : std::shared_ptr<SourceRegistry>{};
        const std::uint8_t levelMaskBit = levelBit(entry.level);
        if (!entry.bypassFilters &&
            (levelMaskBit == 0 ||
             (runtimeStateLevelMask(runtimeState) & levelMaskBit) == 0 ||
             (entry.usesRegisteredSource && !sourceEnabledRuntime(registry.get(), entry.sourceId))))
        {
            return result;
        }

        const bool consoleOutput = hasConsoleOutput(mode);
        const bool wantsFileOutput = hasFileOutput(mode);
        if (!consoleOutput && !wantsFileOutput)
        {
            return result;
        }

        const LogStyle style = getLogStyle(entry.level);
        bool unknownSource = false;
        const std::string_view source = resolveSourceText(entry, registry.get(), unknownSource);
        buildLogLine(lineScratch, getTimestampText(timestampCache), style.text, source, entry.message.view());

        if (consoleOutput)
        {
            std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
            std::ostream &consoleStream = style.useCerr ? std::cerr : std::cout;
            if (consoleColorEnabledForStream(style.useCerr) && style.color[0] != '\0')
            {
                consoleStream << style.color << lineScratch << "\033[0m";
            }
            else
            {
                consoleStream << lineScratch;
            }

            consoleStream << '\n';
            if (loggerState.flushConsoleEveryWriteAtomic.load(std::memory_order_acquire))
            {
                consoleStream.flush();
            }
            result.acceptedImmediateSink = !consoleStream.fail();
        }

        if (wantsFileOutput)
        {
            if (loggerState.fileOutputAvailableAtomic.load(std::memory_order_acquire))
            {
                fileBatchScratch.append(lineScratch);
                fileBatchScratch.push_back('\n');
                result.queuedFile = true;
            }
            else
            {
                recordFileWriteFailure(PlatformError{PlatformErrorSource::File, 0});
            }
        }

        if (unknownSource)
        {
            recordUnknownSourceUse();
        }

        return result;
    }

    /// @brief Writes the worker file batch to disk and optionally flushes it.
    /// @param fileBatchScratch Batched file text to write; cleared before return.
    /// @param forceFlush True when shutdown/flush path requires an immediate file flush.
    /// @return True when the batch was empty or written successfully.
    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush)
    {
        if (fileBatchScratch.empty())
        {
            return true;
        }

        std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
        bool success = false;
        PlatformError fileError = PlatformError{PlatformErrorSource::File, 0};
        if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
        {
            fileError = writeFileForLogger(loggerState.logFile, fileBatchScratch);
            if (forceFlush || loggerState.flushFileEveryBatchAtomic.load(std::memory_order_acquire))
            {
                if (!hasPlatformError(fileError))
                {
                    fileError = flushFileForLogger(loggerState.logFile);
                }
            }
            success = !hasPlatformError(fileError);
        }

        fileBatchScratch.clear();
        if (!success)
        {
            recordFileWriteFailure(fileError);
        }

        return success;
    }

    //-------------------------------------------------------------------------------------------------
    // Worker helpers
    //-------------------------------------------------------------------------------------------------

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
                std::unique_lock<std::mutex> lock(loggerState.logMutex);
                loggerState.logCondition.wait(lock, []
                                              { return loggerState.publishedQueueDepth.load(std::memory_order_acquire) > 0 ||
                                                       (!loggerState.workerRunning &&
                                                        loggerState.activeProducers.load(std::memory_order_acquire) == 0 &&
                                                        loggerState.queueDepth.load(std::memory_order_acquire) == 0); });

                if (loggerState.publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
                    loggerState.queueDepth.load(std::memory_order_acquire) == 0 &&
                    loggerState.activeProducers.load(std::memory_order_acquire) == 0 &&
                    !loggerState.workerRunning)
                {
                    break;
                }
                loggerState.workerBusy = true;
            }

            batchCount = drainQueueBatch(loggerState.workerBatch);
            if (batchCount == 0)
            {
                {
                    std::lock_guard<std::mutex> lock(loggerState.logMutex);
                    loggerState.workerBusy = false;
                }
                loggerState.logCondition.notify_all();
                std::this_thread::yield();
                continue;
            }

            std::size_t writtenCount = 0;
            std::size_t filePendingCount = 0;
            for (std::size_t index = 0; index < batchCount; ++index)
            {
                const QueuedLogEntry &entry = loggerState.workerBatch[index];
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
                clearLogEntry(loggerState.workerBatch[index]);
            }

            if (loggerState.releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
            {
                std::string{}.swap(lineScratch);
                std::string{}.swap(fileBatchScratch);
            }

            {
                std::lock_guard<std::mutex> lock(loggerState.logMutex);
                loggerState.stats.written.fetch_add(writtenCount, std::memory_order_relaxed);
                loggerState.workerBusy = false;
            }

            loggerState.logCondition.notify_all();
        }
    }

    /// @brief atexit callback that delegates to the idempotent public shutdown path.
    void shutdownLoggerAtExit()
    {
        GameWIP::Logger::shutdown();
    }

    //-------------------------------------------------------------------------------------------------
    // Init and lifecycle state helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Resets init-scoped runtime state while preserving allocated inputs from init().
    /// @param config Startup configuration.
    /// @param softQueueSize Sanitized soft queue size.
    /// @param hardQueueSize Sanitized hard queue size.
    /// @param hardQueueMultiplier Sanitized hard queue multiplier.
    /// @param messageLength Sanitized maximum message length.
    /// @param inlineMessageCapacity Per-slot reserved message bytes.
    /// @param workerBatchSize Effective worker batch capacity.
    /// @param levelMask Initial runtime level-filter mask.
    /// @param sourceRegistry Source registry snapshot to publish.
    /// @param ring Allocated ring storage.
    /// @param ringSize Number of slots in ring.
    /// @param batch Allocated worker batch storage.
    /// @param ringArena Ring message arena.
    /// @param batchArena Worker batch message arena.
    /// @pre loggerState.logMutex is held.
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
        std::unique_ptr<char[]> &&batchArena)
    {
        loggerState.logFilePath.clear();
        loggerState.mode = config.output;
        loggerState.fallbackToConsoleOnFileFailure = config.fallbackToConsoleOnFileFailure;
        loggerState.minLevel = config.minLevel;
        loggerState.softQueueSize = softQueueSize;
        loggerState.hardQueueSize = hardQueueSize;
        loggerState.hardQueueMultiplier = hardQueueMultiplier;
        loggerState.maxMessageLength = messageLength;
        loggerState.formatPolicy = sanitizeFormatPolicy(config.formatPolicy);
        loggerState.inlineMessageCapacity = inlineMessageCapacity;
        loggerState.workerBatchSize = workerBatchSize;
        loggerState.consoleColorEnabled = config.enableConsoleColor;
        loggerState.debugOutputEnabled = config.enableDebugOutput;
        loggerState.fatalPopupEnabled = config.enableFatalPopup;
        loggerState.flushFileEveryBatch = config.flushFileEveryBatch;
        loggerState.flushConsoleEveryWrite = config.flushConsoleEveryWrite;
        loggerState.releaseMessageMemoryAfterWrite = config.releaseMessageMemoryAfterWrite;
        loggerState.releaseStorageOnShutdown = config.releaseStorageOnShutdown;
        loggerState.enabledLevelMask = levelMask;
        loggerState.sourceRegistry.store(std::move(sourceRegistry), std::memory_order_release);
        loggerState.ringMessageArena = std::move(ringArena);
        loggerState.batchMessageArena = std::move(batchArena);
        loggerState.logRing = std::move(ring);
        loggerState.logRingSize = ringSize;
        loggerState.workerBatch = std::move(batch);
        loggerState.droppedLogs.store(0, std::memory_order_relaxed);
        resetAtomicStats();
        loggerState.workerRunning = false;
        loggerState.workerBusy = false;
        loggerState.enqueueTicket.store(0, std::memory_order_relaxed);
        loggerState.dequeueTicket.store(0, std::memory_order_relaxed);
        loggerState.queueDepth.store(0, std::memory_order_relaxed);
        loggerState.publishedQueueDepth.store(0, std::memory_order_relaxed);
        loggerState.maxMessageLengthAtomic.store(messageLength, std::memory_order_release);
        loggerState.formatPolicyAtomic.store(toFormatPolicyValue(loggerState.formatPolicy), std::memory_order_release);
        loggerState.releaseMessageMemoryAfterWriteAtomic.store(config.releaseMessageMemoryAfterWrite, std::memory_order_release);
        publishRuntimeStateUnlocked();
        publishConsoleColorSupport();
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
    }

    /// @brief Changes output mode after file setup fallback.
    /// @param mode New output mode.
    /// @note Used during init before worker start; it also updates runtime output atomics.
    void setOutputMode(OutputMode mode)
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.mode = mode;
        publishRuntimeStateUnlocked();
        if (!hasFileOutput(mode))
        {
            loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }
    }

    /// @brief Chooses the remaining output mode when the requested file sink cannot be opened.
    /// @param requested Startup output requested by Config.
    /// @param fallbackToConsole True to use Console for file-only failures.
    /// @return Console when explicitly requested or allowed as fallback; otherwise None.
    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole)
    {
        if (hasConsoleOutput(requested) || fallbackToConsole)
        {
            return OutputMode::Console;
        }

        return OutputMode::None;
    }

    //-------------------------------------------------------------------------------------------------
    // Enqueue helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Applies queue policy and enqueues one pending entry if accepted.
    /// @param entry Pending entry to enqueue.
    /// @return Post-unlock side effects for worker wake.
    /// @note Queue policy intentionally drops every severity at hardQueueSize.
    EnqueueOutcome enqueuePendingLogEntry(const PendingLogEntry &entry, bool countDrops = true)
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
                recordQueueDropCounter(loggerState.stats.queueDropsHard);
            }
            result.status = EnqueueStatus::DroppedHard;
            return result;
        }

        if (reserveStatus == EnqueueStatus::DroppedSoft)
        {
            if (countDrops)
            {
                recordQueueDropCounter(loggerState.stats.queueDropsSoft);
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
                recordQueueDropCounter(loggerState.stats.queueDropsHard);
            }
            result.status = EnqueueStatus::DroppedHard;
            return result;
        }

        loggerState.stats.queued.fetch_add(1, std::memory_order_relaxed);
        if (entryWasTruncated)
        {
            loggerState.stats.truncated.fetch_add(1, std::memory_order_relaxed);
        }

        updateAtomicMax(loggerState.stats.peakQueueDepth, previousDepth + 1);
        result.notifyWorker = notifyWorker;
        result.status = EnqueueStatus::Queued;
        return result;
    }

    /// @brief Builds a borrowed-message pending entry with a string source.
    /// @param level Entry severity.
    /// @param source Source text to copy into the pending entry.
    /// @param message Message view copied into the ring slot before the public call returns.
    /// @param bypassFilters True for report paths that ignore min-level and runtime filters.
    /// @return Pending producer-side entry.
    PendingLogEntry makePendingEntry(LogLevel level, std::string_view source, std::string_view message, bool bypassFilters = false, bool alreadyTruncated = false)
    {
        PendingLogEntry entry;
        entry.level = level;
        entry.bypassFilters = bypassFilters;
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
    /// @param bypassFilters True for report paths that ignore min-level and runtime filters.
    /// @return Pending producer-side entry.
    PendingLogEntry makePendingEntry(LogLevel level, SourceId source, std::string_view message, bool bypassFilters = false, bool alreadyTruncated = false)
    {
        PendingLogEntry entry;
        entry.level = level;
        entry.bypassFilters = bypassFilters;
        entry.usesRegisteredSource = true;
        entry.sourceId = source;
        entry.sourceText.clear();
        entry.message = message;
        entry.alreadyTruncated = alreadyTruncated;
        return entry;
    }

    /// @brief Enqueues a pending entry and wakes the worker when needed.
    /// @param entry Pending entry whose borrowed message remains valid for this call.
    EnqueueOutcome enqueueAndWakeWorker(const PendingLogEntry &entry, bool countDrops = true)
    {
        const EnqueueOutcome enqueueResult = enqueuePendingLogEntry(entry, countDrops);

        if (enqueueResult.notifyWorker)
        {
            loggerState.logCondition.notify_one();
        }

        return enqueueResult;
    }

    /// @brief Shows the fatal popup when enabled.
    /// @param message Fatal popup message text.
    /// @note This remains internal; public fatal popup behavior goes through reportFatal().
    void showFatalPopupIfEnabled(std::string_view message)
    {
        if (!loggerState.fatalPopupEnabledAtomic.load(std::memory_order_acquire))
        {
            return;
        }

        try
        {
            #if GAMEWIP_LOGGER_TEST_HOOKS
            if (consumeTestHook(loggerTestHookState.nextFatalPopupFailure))
            {
                recordPlatformErrorIfAny(forcedFatalPopupError());
                return;
            }
#endif
            recordPlatformErrorIfAny(GameWIP::LoggerDetail::Platform::showFatalPopup(message));
        }
        catch (...)
        {
            countAllocationFailure();
        }
    }

    /// @brief Flushes active sinks without waiting for async queued work.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    /// @return True when sink flushing did not report a platform file error.
    bool flushSinksInternal()
    {
        bool fileFlushFailed = false;
        PlatformError fileError;
        {
            std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
            std::cout.flush();
            std::cerr.flush();

            if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
            {
                fileError = flushFileForLogger(loggerState.logFile);
                fileFlushFailed = hasPlatformError(fileError);
            }
        }

        if (fileFlushFailed)
        {
            recordFileWriteFailure(fileError);
            return false;
        }
        return true;
    }

    /// @brief Waits for accepted queued work to drain, then flushes active sinks.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    void flushInternal()
    {
        {
            std::unique_lock<std::mutex> lock(loggerState.logMutex);
            loggerState.logCondition.wait(lock, []
                                          { return (!loggerState.workerRunning && !loggerState.workerBusy) || (loggerState.queueDepth.load(std::memory_order_acquire) == 0 && !loggerState.workerBusy); });
        }

        (void)flushSinksInternal();
    }

    /// @brief Waits for accepted queued work to drain until timeout, then flushes active sinks.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    /// @return True when the queue drained and sink flushing succeeded before timeout expired.
    bool flushInternal(std::chrono::milliseconds timeout)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            return false;
        }
#endif
        const bool drained = [&]
        {
            std::unique_lock<std::mutex> lock(loggerState.logMutex);
            return loggerState.logCondition.wait_for(lock, timeout, []
                                                     { return (!loggerState.workerRunning && !loggerState.workerBusy) || (loggerState.queueDepth.load(std::memory_order_acquire) == 0 && !loggerState.workerBusy); });
        }();

        if (!drained)
        {
            return false;
        }

        return flushSinksInternal();
    }

}

#if GAMEWIP_LOGGER_TEST_HOOKS
namespace GameWIP::LoggerDetail::TestHooks
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
}
#endif

//-------------------------------------------------------------------------------------------------
// Public query and lifecycle API
//-------------------------------------------------------------------------------------------------

/// @brief Returns whether the async worker is accepting normal log work.
/// @return True while the worker-running runtime flag is set.
bool GameWIP::Logger::isRunning()
{
    return (loggerState.runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0;
}

/// @brief Returns the configured startup severity floor.
/// @return Minimum level copied during init().
LogLevel GameWIP::Logger::getMinLevel()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.minLevel;
}

/// @brief Returns the active output mode.
/// @return Output mode, including file setup fallback to Console.
OutputMode GameWIP::Logger::getOutput()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.mode;
}

/// @brief Returns the active log file path.
/// @return Current file path, or empty string when no file sink is active.
std::string GameWIP::Logger::getLogFilePath()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.logFilePath.string();
}

/// @brief Returns the effective queue and message limits selected during init.
/// @return Queue and message limit snapshot.
QueueLimits GameWIP::Logger::getQueueLimits()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return QueueLimits{
        loggerState.softQueueSize,
        loggerState.hardQueueSize,
        loggerState.hardQueueMultiplier,
        loggerState.maxMessageLength,
        loggerState.inlineMessageCapacity,
        loggerState.workerBatchSize};
}

/// @brief Returns lifetime queue-pressure drop count.
/// @return Queue-drop count preserved for diagnostics.
std::size_t GameWIP::Logger::getLifetimeDroppedLogCount()
{
    return loggerState.droppedLogs.load(std::memory_order_relaxed);
}

/// @brief Returns the last logger result.
/// @return Last result recorded by init, filters, or sink failure handling.
LoggerResult GameWIP::Logger::getLastResult()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.lastResult;
}

/// @brief Returns the last platform error details.
/// @return Last platform error, or source None when no platform error is recorded.
GameWIP::Logger::Types::PlatformError GameWIP::Logger::getLastPlatformError()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    return loggerState.lastPlatformError;
}

/// @brief Returns a snapshot of resettable stats counters.
/// @return Relaxed atomic stats snapshot for diagnostics.
LoggerStats GameWIP::Logger::getStats()
{
    return snapshotStats();
}

/// @brief Returns a cold memory diagnostic snapshot.
/// @return Best-effort retained logger memory plus process memory when available.
LoggerMemoryStats GameWIP::Logger::getMemoryStats()
{
    LoggerMemoryStats memory;
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        memory.queueStorageBytes = queueStorageBytesUnlocked();
        memory.messageArenaBytes = messageArenaBytesUnlocked();
        memory.sourceRegistryBytes = publishedSourceRegistryBytes();
        memory.entryTextHeapCapacityAvailable = entryTextHeapCapacityAvailableUnlocked();
        memory.entryTextHeapCapacityBytes = memory.entryTextHeapCapacityAvailable ? entryTextHeapCapacityBytesUnlocked() : 0;
        memory.loggerRetainedBytes =
            sizeof(LoggerState) +
            memory.queueStorageBytes +
            memory.messageArenaBytes +
            memory.sourceRegistryBytes +
            memory.entryTextHeapCapacityBytes;
    }

    const GameWIP::LoggerDetail::Platform::ProcessMemory processMemory = GameWIP::LoggerDetail::Platform::queryProcessMemory();
    memory.processWorkingSetBytes = processMemory.workingSetBytes;
    memory.processPrivateBytes = processMemory.privateBytes;
    memory.processMemoryAvailable = processMemory.available;
    return memory;
}

/// @brief Resets visible stats counters while preserving lifetime queue-drop state.
/// @note Shutdown reporting uses droppedLogs, so resetStats() must not clear the lifetime queue-drop value.
void GameWIP::Logger::resetStats()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    resetAtomicStats(loggerState.queueDepth.load(std::memory_order_acquire));
}

/// @brief Builds the default startup configuration.
GameWIP::Logger::Types::Config GameWIP::Logger::defaultConfig()
{
    return Types::Config{};
}

/// @brief Builds a lower-retained-memory startup configuration.
GameWIP::Logger::Types::Config GameWIP::Logger::lowMemoryConfig()
{
    Types::Config config;
    config.maxQueueSize = 256;
    config.hardQueueMultiplier = 1.0;
    config.maxMessageLength = 1024;
    config.inlineMessageCapacity = 128;
    config.workerBatchSize = 64;
    config.releaseMessageMemoryAfterWrite = true;
    config.releaseStorageOnShutdown = true;
    return config;
}

/// @brief Builds a higher-throughput startup configuration.
GameWIP::Logger::Types::Config GameWIP::Logger::throughputConfig()
{
    Types::Config config;
    config.maxQueueSize = 4096;
    config.hardQueueMultiplier = 1.25;
    config.maxMessageLength = 4096;
    config.inlineMessageCapacity = 256;
    config.workerBatchSize = 512;
    config.releaseMessageMemoryAfterWrite = false;
    config.releaseStorageOnShutdown = false;
    return config;
}

/// @brief Starts the logger using defaultConfig().
LoggerResult GameWIP::Logger::initDefault()
{
    return init(defaultConfig());
}

/// @brief Starts a console-only logger.
LoggerResult GameWIP::Logger::initConsole(Types::Level minLevel)
{
    Types::Config config = defaultConfig();
    config.output = OutputMode::Console;
    config.minLevel = minLevel;
    return init(config);
}

/// @brief Starts a file-only logger.
LoggerResult GameWIP::Logger::initFile(std::string_view directory, Types::Level minLevel)
{
    Types::Config config = defaultConfig();
    config.output = OutputMode::File;
    config.minLevel = minLevel;
    config.logDirectory = directory;
    return init(config);
}

/// @brief Initializes the async logger and starts its worker thread.
/// @param config Startup configuration.
/// @return Success or the first non-fatal setup/configuration result.
LoggerResult GameWIP::Logger::init(const Types::Config &config)
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState.lifecycleMutex);

    bool alreadyRunning = false;
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        alreadyRunning = loggerState.workerRunning || loggerState.loggingThread.joinable();
        if (alreadyRunning)
        {
            setResultUnlocked(LoggerResult::AlreadyRunning);
        }
    }

    if (alreadyRunning)
    {
        return LoggerResult::AlreadyRunning;
    }

    if (!loggerState.shutdownRegistered)
    {
        std::atexit(shutdownLoggerAtExit);
        loggerState.shutdownRegistered = true;
    }

    LoggerResult initResult = LoggerResult::Success;
    PlatformError initPlatformError;

    if (!isValidOutputMode(config.output))
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.mode = OutputMode::None;
        loggerState.workerRunning = false;
        loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(LoggerResult::InvalidOutputMode);
        return LoggerResult::InvalidOutputMode;
    }

    std::size_t softQueueSize = config.maxQueueSize;
    if (softQueueSize == 0)
    {
        softQueueSize = 4;
        preserveFirstInitResult(initResult, LoggerResult::InvalidQueueSize, initPlatformError);
    }

    std::size_t maxMessageLength = config.maxMessageLength;
    if (maxMessageLength == 0)
    {
        maxMessageLength = 512;
        preserveFirstInitResult(initResult, LoggerResult::InvalidMessageLength, initPlatformError);
    }

    const std::size_t inlineMessageCapacity = std::min(config.inlineMessageCapacity, maxMessageLength);
    double hardQueueMultiplier = config.hardQueueMultiplier;
    std::size_t hardQueueSize = effectiveHardQueueLimit(hardQueueMultiplier, softQueueSize, initResult);
    std::size_t workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, hardQueueSize);

    if (!isValidLevel(config.minLevel))
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.mode = OutputMode::None;
        loggerState.workerRunning = false;
        loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(LoggerResult::InvalidLevelFilter);
        return LoggerResult::InvalidLevelFilter;
    }

    std::uint8_t levelMask = kAllLevelMask;
    if (!prepareLevelMask(config.levelFilters, levelMask))
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.mode = OutputMode::None;
        loggerState.workerRunning = false;
        loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(LoggerResult::InvalidLevelFilter);
        return LoggerResult::InvalidLevelFilter;
    }

    LoggerResult sourcePrepareResult = LoggerResult::Success;
    std::shared_ptr<SourceRegistry> sourceRegistry;
    if (!prepareSources(config.sources, config.sourceFilters, sourceRegistry, sourcePrepareResult))
    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.mode = OutputMode::None;
        loggerState.workerRunning = false;
        loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(sourcePrepareResult);
        return sourcePrepareResult;
    }

    if (config.output == OutputMode::None)
    {
        Types::Config disabledConfig = config;
        disabledConfig.output = OutputMode::None;

        {
            std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
            if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
            {
                GameWIP::LoggerDetail::Platform::closeFile(loggerState.logFile);
                loggerState.logFile = {};
            }
            loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }

        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        resetRuntimeStateUnlocked(
            disabledConfig,
            softQueueSize,
            hardQueueSize,
            hardQueueMultiplier,
            maxMessageLength,
            inlineMessageCapacity,
            workerBatchSize,
            levelMask,
            std::move(sourceRegistry),
            std::unique_ptr<QueueSlot[]>{},
            0,
            std::vector<QueuedLogEntry>{},
            std::unique_ptr<char[]>{},
            std::unique_ptr<char[]>{});
        setResultUnlocked(initResult, initPlatformError);
        return initResult;
    }

    {
        std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
        if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
        {
            GameWIP::LoggerDetail::Platform::closeFile(loggerState.logFile);
            loggerState.logFile = {};
        }
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        loggerState.stdoutColorEnabledAtomic.store(false, std::memory_order_release);
        loggerState.stderrColorEnabledAtomic.store(false, std::memory_order_release);
    }

    std::unique_ptr<QueueSlot[]> ring;
    std::size_t ringSize = 0;
    std::vector<QueuedLogEntry> batch;
    std::unique_ptr<char[]> ringArena;
    std::unique_ptr<char[]> batchArena;
    if (!prepareQueueStorage(hardQueueSize, workerBatchSize, inlineMessageCapacity, ring, ringSize, batch, ringArena, batchArena))
    {
        softQueueSize = 4;
        hardQueueSize = softQueueSize;
        hardQueueMultiplier = 1.0;
        workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, hardQueueSize);
        if (!prepareQueueStorage(hardQueueSize, workerBatchSize, inlineMessageCapacity, ring, ringSize, batch, ringArena, batchArena))
        {
            std::lock_guard<std::mutex> lock(loggerState.logMutex);
            loggerState.mode = OutputMode::None;
            loggerState.workerRunning = false;
            loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
            publishRuntimeStateUnlocked();
            loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
            setResultUnlocked(LoggerResult::InvalidQueueSize);
            return LoggerResult::InvalidQueueSize;
        }

        preserveFirstInitResult(initResult, LoggerResult::InvalidQueueSize, initPlatformError);
    }

    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        resetRuntimeStateUnlocked(
            config,
            softQueueSize,
            hardQueueSize,
            hardQueueMultiplier,
            maxMessageLength,
            inlineMessageCapacity,
            workerBatchSize,
            levelMask,
            std::move(sourceRegistry),
            std::move(ring),
            ringSize,
            std::move(batch),
            std::move(ringArena),
            std::move(batchArena));
        setResultUnlocked(initResult);
    }

    const bool wantsFile = hasFileOutput(config.output);
    bool fileSetupFailed = false;

    if (wantsFile)
    {
        try
        {
            const std::string logDirectoryText = config.logDirectory.empty() ? std::string(LOGGER_DEFAULT_DIRECTORY) : std::string(config.logDirectory);

            if (logDirectoryText.empty())
            {
                setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
                preserveFirstInitResult(initResult, LoggerResult::InvalidLogDirectory, initPlatformError);
                fileSetupFailed = true;
            }
            else
            {
                const PlatformError directoryError = GameWIP::LoggerDetail::Platform::createDirectories(logDirectoryText);
                if (hasPlatformError(directoryError))
                {
                    setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
                    preserveFirstInitResult(initResult, LoggerResult::FileSetupFailed, initPlatformError, directoryError);
                    fileSetupFailed = true;
                }
            }

            if (!fileSetupFailed)
            {
                PlatformError timeError;
                const std::string logFileBaseName = getCurrentTimeText("%Y-%m-%d_%H-%M-%S", timeError);
                if (hasPlatformError(timeError))
                {
                    preserveFirstInitResult(initResult, LoggerResult::PlatformCallFailed, initPlatformError, timeError);
                }

                constexpr std::size_t kMaxCollisionAttempts = 1024;
                bool opened = false;
                PlatformError lastOpenError;
                {
                    std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
                    for (std::size_t index = 0; index <= kMaxCollisionAttempts; ++index)
                    {
                        const std::string fileName = index == 0
                                                         ? std::string(logFileBaseName) + ".log"
                                                         : std::string(logFileBaseName) + "_" + std::to_string(index) + ".log";
                        std::string nativeCandidatePath = logDirectoryText;
                        if (!nativeCandidatePath.empty() && nativeCandidatePath.back() != '/' && nativeCandidatePath.back() != '\\')
                        {
                            nativeCandidatePath.push_back('\\');
                        }
                        nativeCandidatePath.append(fileName);
                        FileHandle candidateHandle;
                        const PlatformError openError = openFileExclusiveForLogger(nativeCandidatePath, candidateHandle);
                        lastOpenError = openError;
                        if (!hasPlatformError(openError))
                        {
                            loggerState.logFile = candidateHandle;
                            {
                                std::lock_guard<std::mutex> lock(loggerState.logMutex);
                                loggerState.logFilePath = nativeCandidatePath;
                            }
                            loggerState.fileOutputAvailableAtomic.store(true, std::memory_order_release);
                            opened = true;
                            break;
                        }
                    }
                }

                if (!opened)
                {
                    setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
                    preserveFirstInitResult(initResult, LoggerResult::FileOpenFailed, initPlatformError, lastOpenError);
                    fileSetupFailed = true;
                }
            }
        }
        catch (const std::exception &)
        {
            setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
            preserveFirstInitResult(initResult, LoggerResult::FileSetupFailed, initPlatformError);
            fileSetupFailed = true;
        }
        catch (...)
        {
            setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
            preserveFirstInitResult(initResult, LoggerResult::FileSetupFailed, initPlatformError);
            fileSetupFailed = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        if (loggerState.mode == OutputMode::None)
        {
            clearQueueUnlocked();
            if (loggerState.releaseStorageOnShutdown)
            {
                releaseRuntimeStorageUnlocked();
            }
            publishRuntimeStateUnlocked();
            setResultUnlocked(initResult, initPlatformError);
            return initResult;
        }

        loggerState.workerRunning = true;
        loggerState.workerBusy = false;
        publishRuntimeStateUnlocked();
    }

    try
    {
        loggerState.loggingThread = std::thread(loggerWorker);
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(loggerState.logMutex);
            loggerState.workerRunning = false;
            loggerState.workerBusy = false;
            loggerState.mode = OutputMode::None;
            loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
            publishRuntimeStateUnlocked();
            loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
            setResultUnlocked(LoggerResult::ThreadStartFailed);
        }
        waitForActiveProducersToLeave();

        {
            std::lock_guard<std::mutex> lock(loggerState.logMutex);
            clearQueueUnlocked();
            if (loggerState.releaseStorageOnShutdown)
            {
                releaseRuntimeStorageUnlocked();
            }
        }

        {
            std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
            if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
            {
                GameWIP::LoggerDetail::Platform::closeFile(loggerState.logFile);
                loggerState.logFile = {};
            }
        }

        {
            std::lock_guard<std::mutex> lock(loggerState.logMutex);
            loggerState.logFilePath.clear();
        }

        return LoggerResult::ThreadStartFailed;
    }

    recordResult(initResult, initPlatformError);

    return initResult;
}

//-------------------------------------------------------------------------------------------------
// Public filter API
//-------------------------------------------------------------------------------------------------

/// @brief Tests whether a severity-only log would currently be accepted.
/// @param level Level to test.
/// @return True when output/running/minLevel/runtime-level filters allow the log.
bool GameWIP::Logger::shouldLog(LogLevel level)
{
    return shouldLogRuntime(level);
}

/// @brief Tests whether a string source log would currently be accepted.
bool GameWIP::Logger::shouldLog(LogLevel level, std::string_view)
{
    return shouldLogRuntime(level);
}

/// @brief Tests whether a registered source log would currently be accepted.
/// @param level Level to test.
/// @param source SourceId to test.
/// @return True when shouldLog(level) passes and runtime source filters allow the source.
bool GameWIP::Logger::shouldLog(LogLevel level, SourceId source)
{
    return shouldLogRuntime(level, source);
}

/// @brief Enables or disables one registered source at runtime.
/// @param source SourceId to change.
/// @param enabled True to enable the source, false to filter it out.
/// @return Success or InvalidSourceFilter for unknown sources.
LoggerResult GameWIP::Logger::setSourceFilter(SourceId source, bool enabled)
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
    const RegisteredSource *registeredSource = registry ? findSource(*registry, source) : nullptr;
    if (registeredSource == nullptr)
    {
        setResultUnlocked(LoggerResult::InvalidSourceFilter);
        return LoggerResult::InvalidSourceFilter;
    }

    registeredSource->enabled.store(enabled, std::memory_order_release);
    setResultUnlocked(LoggerResult::Success);
    return LoggerResult::Success;
}

/// @brief Clears one source filter by enabling that source.
/// @param source SourceId to enable.
/// @return Success or InvalidSourceFilter for unknown sources.
LoggerResult GameWIP::Logger::clearSourceFilter(SourceId source)
{
    return setSourceFilter(source, true);
}

/// @brief Clears all source filters by enabling every registered source.
void GameWIP::Logger::clearSourceFilters()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
    if (!registry)
    {
        setResultUnlocked(LoggerResult::Success);
        return;
    }

    for (const RegisteredSource &source : registry->sources)
    {
        source.enabled.store(true, std::memory_order_release);
    }
    setResultUnlocked(LoggerResult::Success);
}

/// @brief Enables or disables one exact severity level at runtime.
/// @param level Exact level to change.
/// @param enabled True to enable the level, false to filter it out.
/// @return Success or InvalidLevelFilter for invalid enum values.
LoggerResult GameWIP::Logger::setLevelFilter(LogLevel level, bool enabled)
{
    const std::uint8_t bit = levelBit(level);
    if (bit == 0)
    {
        recordResult(LoggerResult::InvalidLevelFilter);
        return LoggerResult::InvalidLevelFilter;
    }

    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    if (enabled)
    {
        loggerState.enabledLevelMask = static_cast<std::uint8_t>(loggerState.enabledLevelMask | bit);
    }
    else
    {
        loggerState.enabledLevelMask = static_cast<std::uint8_t>(loggerState.enabledLevelMask & static_cast<std::uint8_t>(~bit));
    }

    publishRuntimeStateUnlocked();
    setResultUnlocked(LoggerResult::Success);
    return LoggerResult::Success;
}

/// @brief Clears one level filter by enabling that exact level.
/// @param level Exact level to enable.
/// @return Success or InvalidLevelFilter for invalid enum values.
LoggerResult GameWIP::Logger::clearLevelFilter(LogLevel level)
{
    return setLevelFilter(level, true);
}

/// @brief Clears all level filters by enabling every valid level.
void GameWIP::Logger::clearLevelFilters()
{
    std::lock_guard<std::mutex> lock(loggerState.logMutex);
    loggerState.enabledLevelMask = kAllLevelMask;
    publishRuntimeStateUnlocked();
    setResultUnlocked(LoggerResult::Success);
}

//-------------------------------------------------------------------------------------------------
// Public flush and shutdown API
//-------------------------------------------------------------------------------------------------

/// @brief Waits until the queue drains, then flushes console and file sinks.
void GameWIP::Logger::flush()
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState.lifecycleMutex);
    flushInternal();
}

/// @brief Waits until the queue drains or timeout expires, then flushes console and file sinks.
/// @param timeout Maximum wait duration.
/// @return True when queued work drained and sink flushing succeeded before timeout expired.
bool GameWIP::Logger::flush(std::chrono::milliseconds timeout)
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState.lifecycleMutex);
    return flushInternal(timeout);
}

/// @brief Stops the worker, drains queued logs, and closes the file sink.
/// @note Safe to call repeatedly and safe before init().
void GameWIP::Logger::shutdown()
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState.lifecycleMutex);

    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.workerRunning = false;
        publishRuntimeStateUnlocked();
    }

    loggerState.logCondition.notify_all();

    if (loggerState.loggingThread.joinable())
    {
        loggerState.loggingThread.join();
    }

    flushInternal();

    {
        std::lock_guard<std::mutex> outputLock(loggerState.outputMutex);
        if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState.logFile))
        {
            GameWIP::LoggerDetail::Platform::closeFile(loggerState.logFile);
            loggerState.logFile = {};
        }
        loggerState.fileOutputAvailableAtomic.store(false, std::memory_order_release);
        loggerState.stdoutColorEnabledAtomic.store(false, std::memory_order_release);
        loggerState.stderrColorEnabledAtomic.store(false, std::memory_order_release);
    }

    {
        std::lock_guard<std::mutex> lock(loggerState.logMutex);
        loggerState.logFilePath.clear();
        loggerState.mode = OutputMode::None;
        loggerState.workerBusy = false;
        loggerState.sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        clearQueueUnlocked();
        if (loggerState.releaseStorageOnShutdown)
        {
            releaseRuntimeStorageUnlocked();
        }
        publishRuntimeStateUnlocked();
    }
}

//-------------------------------------------------------------------------------------------------
// Private Logger bridge accounting helpers
//-------------------------------------------------------------------------------------------------

/// @brief Counts an allocation/internal-format failure from public template catch paths.
void GameWIP::Logger::recordAllocationFailure()
{
    if ((loggerState.runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
    {
        countAllocationFailure();
    }
}

/// @brief Counts an invalid runtime format failure from public template catch paths.
void GameWIP::Logger::recordFormatFailure()
{
    if ((loggerState.runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
    {
        countFormatFailure();
    }
}

/// @brief Returns reusable per-thread format storage for header-only formatting overloads.
/// @return Per-thread scratch string.
std::string &GameWIP::Logger::formatScratch()
{
#if defined(_WIN32) && defined(__MINGW32__)
    return formatScratchForThread();
#else
    thread_local std::string scratch;
    return scratch;
#endif
}

/// @brief Returns the active maximum message length for header-only bounded formatting.
/// @return Current max message length.
std::size_t GameWIP::Logger::getMaxMessageLengthForFormatting()
{
    return loggerState.maxMessageLengthAtomic.load(std::memory_order_acquire);
}

/// @brief Returns the active formatted-message memory/speed policy.
/// @return Current policy used by header-only formatting overloads.
FormatPolicy GameWIP::Logger::getFormatPolicyForFormatting()
{
    return formatPolicyFromValue(loggerState.formatPolicyAtomic.load(std::memory_order_acquire));
}

/// @brief Releases thread-local formatting scratch capacity when the active config requests it.
/// @param scratch Scratch string to optionally shrink.
void GameWIP::Logger::releaseFormatScratchIfNeeded(std::string &scratch)
{
    if (!loggerState.releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
    {
        return;
    }

    const std::size_t maxMessageLength = loggerState.maxMessageLengthAtomic.load(std::memory_order_acquire);
    if (scratch.capacity() > maxMessageLength)
    {
        std::string{}.swap(scratch);
        return;
    }

    scratch.clear();
}

//-------------------------------------------------------------------------------------------------
// Private Logger bridge enqueue helpers used by header-only formatting overloads
//-------------------------------------------------------------------------------------------------

/// @brief Enqueues a preformatted message after the caller's fast-path filter check.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message)
{
    enqueuePreformattedMessage(level, source, message, false);
}

/// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param alreadyTruncated True when message already includes the truncation suffix.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool alreadyTruncated)
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, false, alreadyTruncated));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

/// @brief Enqueues a preformatted message after the caller's fast-path filter check.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Formatted message copied before this call returns.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message)
{
    enqueuePreformattedMessage(level, source, message, false);
}

/// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Formatted message copied before this call returns.
/// @param alreadyTruncated True when message already includes the truncation suffix.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated)
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, false, alreadyTruncated));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

//-------------------------------------------------------------------------------------------------
// Public logging API
//-------------------------------------------------------------------------------------------------

/// @brief Logs a preformatted message with a string source.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::log(LogLevel level, std::string_view source, std::string_view message)
{
    if (!shouldLog(level))
    {
        return;
    }

    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

/// @brief Logs a preformatted message with a registered SourceId.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::log(LogLevel level, Types::SourceId source, std::string_view message)
{
    if (!shouldLog(level, source))
    {
        return;
    }

    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

/// @brief Logs a Trace message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::trace(std::string_view source, std::string_view message)
{
    log(LogLevel::Trace, source, message);
}

/// @brief Logs a Trace message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::trace(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Trace, source, message);
}

/// @brief Logs a Debug message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::debug(std::string_view source, std::string_view message)
{
    log(LogLevel::Debug, source, message);
}

/// @brief Logs a Debug message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::debug(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Debug, source, message);
}

/// @brief Logs an Info message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::info(std::string_view source, std::string_view message)
{
    log(LogLevel::Info, source, message);
}

/// @brief Logs an Info message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::info(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Info, source, message);
}

/// @brief Logs a Warn message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::warn(std::string_view source, std::string_view message)
{
    log(LogLevel::Warn, source, message);
}

/// @brief Logs a Warn message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::warn(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Warn, source, message);
}

/// @brief Logs an Error message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::error(std::string_view source, std::string_view message)
{
    log(LogLevel::Error, source, message);
}

/// @brief Logs an Error message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::error(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Error, source, message);
}

/// @brief Logs a Fatal message with a string source without forcing platform debug output flush or fatal popup.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::fatal(std::string_view source, std::string_view message)
{
    log(LogLevel::Fatal, source, message);
}

/// @brief Logs a Fatal message with a registered SourceId without forcing platform debug output flush or fatal popup.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::fatal(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Fatal, source, message);
}

//-------------------------------------------------------------------------------------------------
// Reporting and direct platform debug output API
//-------------------------------------------------------------------------------------------------

/// @brief Reports a preformatted message with a string source.
/// @param level Severity to log and mirror.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
void GameWIP::Logger::reportPreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool showPopup)
{
    reportPreformattedMessage(level, source, message, showPopup, false, nullptr);
}

/// @brief Reports a preformatted message with a string source and optional bounded flush.
/// @param level Severity to log and mirror.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
/// @param alreadyTruncated True when message already includes the truncation suffix.
/// @param timeout Optional bounded flush duration.
/// @return True when the flush completed.
bool GameWIP::Logger::reportPreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool showPopup, bool alreadyTruncated, Types::FlushTimeout *timeout)
{
    if (!isValidLevel(level))
    {
        return false;
    }

    std::string boundedMessageScratch;
    bool truncatedNow = false;
    const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
    const bool storedMessageAlreadyTruncated = alreadyTruncated || truncatedNow;

    std::lock_guard<std::mutex> lifecycleLock(loggerState.lifecycleMutex);

    (void)writeReportSynchronously(level, source, reportMessage, false, storedMessageAlreadyTruncated);

    writeDebugOutput(level, source, reportMessage);
    const bool flushed = timeout == nullptr ? flushSinksInternal() : (flushSinksInternal() && flushInternal(timeout->value));

    if (showPopup)
    {
        showFatalPopupIfEnabled(reportMessage);
    }
    return flushed;
}

/// @brief Reports a preformatted message with a registered SourceId.
/// @param level Severity to log and mirror.
/// @param source Registered SourceId to resolve for platform debug output.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
void GameWIP::Logger::reportPreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool showPopup)
{
    reportPreformattedMessage(level, source, message, showPopup, false, nullptr);
}

/// @brief Reports a preformatted message with a registered SourceId and optional bounded flush.
/// @param level Severity to log and mirror.
/// @param source Registered SourceId to resolve for platform debug output.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
/// @param alreadyTruncated True when message already includes the truncation suffix.
/// @param timeout Optional bounded flush duration.
/// @return True when the flush completed.
bool GameWIP::Logger::reportPreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool showPopup, bool alreadyTruncated, Types::FlushTimeout *timeout)
{
    if (!isValidLevel(level))
    {
        return false;
    }

    std::string boundedMessageScratch;
    bool truncatedNow = false;
    const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
    const bool storedMessageAlreadyTruncated = alreadyTruncated || truncatedNow;

    std::lock_guard<std::mutex> lifecycleLock(loggerState.lifecycleMutex);

    (void)writeReportSynchronously(level, source, reportMessage, storedMessageAlreadyTruncated);

    bool unknownSource = false;
    const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
    const std::string_view sourceText = findSourceName(registry.get(), source, unknownSource);
    writeDebugOutput(level, sourceText, reportMessage);
    const bool flushed = timeout == nullptr ? flushSinksInternal() : (flushSinksInternal() && flushInternal(timeout->value));

    if (showPopup)
    {
        showFatalPopupIfEnabled(reportMessage);
    }
    return flushed;
}

/// @brief Synchronously reports a preformatted diagnostic with a string source and no logger-owned popup.
void GameWIP::Logger::report(LogLevel level, std::string_view source, std::string_view message)
{
    reportPreformattedMessage(level, source, message, false);
}

/// @brief Synchronously reports a preformatted diagnostic with a string source and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, false, false, &timeout);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId and no logger-owned popup.
void GameWIP::Logger::report(LogLevel level, SourceId source, std::string_view message)
{
    reportPreformattedMessage(level, source, message, false);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, false, false, &timeout);
}

/// @brief Synchronously reports a preformatted diagnostic with explicit popup behavior.
void GameWIP::Logger::report(LogLevel level, std::string_view source, Types::ReportPopup popup, std::string_view message)
{
    reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal);
}

/// @brief Synchronously reports a preformatted diagnostic with explicit popup behavior and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, std::string_view source, Types::FlushTimeout timeout, Types::ReportPopup popup, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal, false, &timeout);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId and explicit popup behavior.
void GameWIP::Logger::report(LogLevel level, SourceId source, Types::ReportPopup popup, std::string_view message)
{
    reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId, popup behavior, and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, SourceId source, Types::FlushTimeout timeout, Types::ReportPopup popup, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal, false, &timeout);
}

/// @brief Logs an error, mirrors it to platform debug output, and flushes.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::reportError(std::string_view source, std::string_view message)
{
    report(LogLevel::Error, source, message);
}

/// @brief Logs an error, mirrors it to platform debug output, and waits for a bounded flush.
/// @param source Source text to copy.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportError(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Error, source, timeout, message);
}

/// @brief Logs an error with a SourceId, mirrors it to platform debug output, and flushes.
/// @param source Registered SourceId to store and resolve.
/// @param message Message text to copy.
void GameWIP::Logger::reportError(SourceId source, std::string_view message)
{
    report(LogLevel::Error, source, message);
}

/// @brief Logs an error with a SourceId, mirrors it to platform debug output, and waits for a bounded flush.
/// @param source Registered SourceId to store and resolve.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportError(SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Error, source, timeout, message);
}

/// @brief Logs fatal, mirrors it to platform debug output, flushes, and shows the fatal popup when enabled.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::reportFatal(std::string_view source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal, mirrors it to platform debug output, waits for a bounded flush, and shows the fatal popup when enabled.
/// @param source Source text to copy.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportFatal(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal with a SourceId, mirrors it to platform debug output, flushes, and shows the fatal popup when enabled.
/// @param source Registered SourceId to store and resolve.
/// @param message Message text to copy.
void GameWIP::Logger::reportFatal(SourceId source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal with a SourceId, mirrors it to platform debug output, waits for a bounded flush, and shows the fatal popup when enabled.
/// @param source Registered SourceId to store and resolve.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportFatal(SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal, flushes, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Logs fatal with a SourceId, flushes, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(Types::SourceId source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Logs fatal, waits for a bounded flush, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Logs fatal with a SourceId, waits for a bounded flush, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(Types::SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Writes a line directly to the platform debug output when enabled.
/// @param level Severity used for the platform debug output line label.
/// @param source Source text written into the line.
/// @param message Message text written into the line.
void GameWIP::Logger::writeDebugOutput(LogLevel level, std::string_view source, std::string_view message)
{
    if (!loggerState.debugOutputEnabledAtomic.load(std::memory_order_acquire))
    {
        return;
    }

    try
    {
        std::string boundedMessageScratch;
        bool truncated = false;
        const std::string_view messageText = boundedMessageView(message, false, boundedMessageScratch, truncated);
        const LogStyle style = getLogStyle(level);
        std::string line;
        buildLogLine(line, getDebugTimestampText(), style.text, source, messageText);
        line.push_back('\n');
        recordPlatformErrorIfAny(GameWIP::LoggerDetail::Platform::writeDebugOutput(line));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}
