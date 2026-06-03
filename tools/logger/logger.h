/// @file logger.h
/// @brief Public API for the GameWIP Logger library.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "common/export.h"

/// @def LOGGER_API
/// @brief DLL import/export marker used by Logger runtime declarations.
/// @details This is not part of normal user code; use the Logger class API instead.
#if defined(LOGGER_BUILD)
#define LOGGER_API GAMEWIP_SHARED_EXPORT
#else
#define LOGGER_API GAMEWIP_SHARED_IMPORT
#endif

// LOGGER_* convenience macros are intentionally opt-in. Include
// logger/logger_macros.h when global preprocessor logging shortcuts are wanted.

/// @brief Root namespace containing GameWIP public library APIs.
namespace GameWIP
{
    /// @class Logger
    /// @brief Process-wide asynchronous logger for runtime diagnostics.
    ///
    /// Contract:
    /// Normal log calls are bounded, filterable, and queue-backed. Messages are copied before
    /// queueing, so caller-owned text only needs to live through the call. `report()` is the
    /// synchronous diagnostic path: it bypasses runtime filters and the async queue, writes to the
    /// active sinks immediately, mirrors to platform debug output when enabled, and flushes.
    ///
    /// Thread-safety:
    /// Logging calls are safe from multiple producer threads. Lifecycle calls such as `init()`,
    /// `shutdown()`, and public `flush()` calls are serialized with each other.
    ///
    /// Performance:
    /// Formatting happens on the caller thread. Use `shouldLog()` or the optional macros in
    /// `logger_macros.h` around expensive formatting/argument construction.
    ///
    /// See the Logger Markdown manual for setup examples, queue/drop semantics, report behavior,
    /// and the full public API guide.
    class LOGGER_API Logger
    {
    public:
        // Public types and configuration ----------------------------------------------------------
        /// @name Public types and configuration
        /// @{

        /// @brief Public Logger type collection used by configuration, filtering, reporting, and statistics APIs.
        ///
        /// The nested types are grouped here to keep the public namespace compact while still making
        /// the API visible in IntelliSense and Doxygen. See @ref logger for the full how-to manual
        /// and @ref logger_examples for usage examples.
        struct Types
        {
            /// @brief Severity used for startup minLevel checks, runtime level filters, and output styling.
            enum class Level
            {
                Trace,
                Debug,
                Info,
                Warn,
                Error,
                Fatal
            };

            /// @brief Enabled output sinks. File-only setup may fall back to Console when
            /// Config::fallbackToConsoleOnFileFailure is true.
            enum class Output
            {
                None,
                Console,
                File,
                Both
            };

            /// @brief Formatting policy used by formatted logger overloads before queueing.
            enum class FormatPolicy
            {
                /// @brief Retains at most Config::maxMessageLength bytes while formatting; minimizes peak message
                /// memory.
                StrictBounded,
                /// @brief Formats into reusable scratch first, then truncates; faster normal case, higher peak memory
                /// for huge output.
                FastNormal
            };

            /// @brief Operation result for init and runtime filter changes.
            enum class Result
            {
                Success,
                AlreadyRunning,
                InvalidOutputMode,
                InvalidQueueSize,
                InvalidMessageLength,
                InvalidLogDirectory,
                InvalidSourceDefinition,
                InvalidSourceFilter,
                InvalidLevelFilter,
                FileOpenFailed,
                FileWriteFailed,
                FileSetupFailed,
                ThreadStartFailed,
                PlatformCallFailed
            };

            /// @brief Native platform call family that produced PlatformError::nativeCode.
            enum class PlatformErrorSource
            {
                None,
                DebugOutput,
                FatalPopup,
                TimeConversion,
                File
            };

            /// @brief Stable numeric source key stored in hot queued log entries.
            using SourceId = std::uint32_t;

            /// @brief Registers a SourceId name. The logger copies names during init().
            struct SourceDefinition
            {
                /// @brief Stable ID stored in queued enum source log entries.
                SourceId id = 0;
                /// @brief Display name written to logs. Copied by Logger::init().
                std::string_view name = {};
            };

            /// @brief Initial or runtime on/off filter for a registered source.
            struct SourceFilter
            {
                /// @brief Registered source ID affected by this filter.
                SourceId source = 0;
                /// @brief False suppresses this source after the startup minLevel check passes.
                bool enabled = true;
            };

            /// @brief Initial or runtime on/off filter for one exact severity level.
            struct LevelFilter
            {
                /// @brief Exact severity level affected by this filter.
                Level level = Level::Trace;
                /// @brief False suppresses this exact level after the startup minLevel check passes.
                bool enabled = true;
            };

            /// @brief Explicit wrapper for dynamic format strings that cannot be compile-time checked.
            struct RuntimeFormat
            {
                /// @brief Runtime format text passed to std::vformat inside the logger.
                std::string_view text = {};
            };

            /// @brief Explicit wrapper for report/flush APIs that should wait for a bounded duration.
            struct FlushTimeout
            {
                /// @brief Maximum time to wait for queued work and sink flushing.
                std::chrono::milliseconds value{};
            };

            /// @brief Optional popup behavior for the synchronous report path.
            enum class ReportPopup
            {
                /// @brief Write, mirror, and flush the report without using the logger-owned fatal popup.
                None,
                /// @brief Also show the logger-owned fatal popup when Config::enableFatalPopup is true.
                Fatal
            };

            /// @brief Last native platform failure reported by the platform debug output, fatal popup, or time bridge.
            struct PlatformError
            {
                /// @brief Platform operation family that produced nativeCode.
                PlatformErrorSource source = PlatformErrorSource::None;
                /// @brief Native platform error code, such as a Win32 GetLastError value.
                std::uint64_t nativeCode = 0;
            };

            /// @brief Startup configuration copied by init().
            /// @details Runtime changes are intentionally limited to source and level filters.
            /// Changing a Config object after init() has no effect on the running logger.
            struct Config
            {
                /// @brief Enabled sinks for normal log output.
                /// @details Output::File may fall back to Output::Console when fallbackToConsoleOnFileFailure is true.
                Output output = Output::Both;
                /// @brief Startup severity floor.
                /// @details Runtime LevelFilter changes cannot re-enable levels below this floor.
                Level minLevel = Level::Info;
                /// @brief Soft queue limit where low-priority messages may start dropping.
                /// @details This bounds normal burst memory. Error and Fatal messages may continue until the hard
                /// limit.
                std::size_t maxQueueSize = 1024;
                /// @brief Multiplier used to derive the hard queue limit from maxQueueSize.
                /// @details Effective hard limit is ceil(maxQueueSize * hardQueueMultiplier), with validation/fallback
                /// during init().
                double hardQueueMultiplier = 1.25;
                /// @brief Maximum stored message length before truncation.
                /// @details Formatted and preformatted messages longer than this are retained with a truncation suffix.
                std::size_t maxMessageLength = 4096;
                /// @brief Caller-thread formatting memory/speed tradeoff.
                /// @note Formatting happens before queue insertion; use LOGGER_* macros or shouldLog() around expensive
                /// arguments.
                FormatPolicy formatPolicy = FormatPolicy::StrictBounded;
                /// @brief Per-slot preallocated message bytes.
                /// @details Zero disables the inline arena. Values above maxMessageLength are clamped by init().
                std::size_t inlineMessageCapacity = 256;
                /// @brief Worker entries drained per batch.
                /// @details Zero uses the default; the effective value is clamped to the hard queue size.
                std::size_t workerBatchSize = 256;
                /// @brief Optional log directory.
                /// @details Empty uses the build-time default log directory. The path is copied during init().
                std::string_view logDirectory = {};
                /// @brief Allows file-only startup to fall back to console when file setup fails.
                /// @details Output::Both keeps the console sink active even when the file sink fails.
                bool fallbackToConsoleOnFileFailure = true;
                /// @brief Registered source table copied during init() for SourceId logging.
                /// @note Unknown SourceId values are still accepted and reported through Stats::unknownSourceUses.
                std::span<const SourceDefinition> sources = {};
                /// @brief Initial registered source filters applied during init().
                /// @details Filters only affect registered SourceId/enum sources. String sources are severity-filtered
                /// only.
                std::span<const SourceFilter> sourceFilters = {};
                /// @brief Initial exact-level filters applied during init().
                /// @details Filtered levels are skipped intentionally and are not counted as dropped logs.
                std::span<const LevelFilter> levelFilters = {};
                /// @brief Enables ANSI colors for interactive console output.
                bool enableConsoleColor = true;
                /// @brief Enables platform debug output for writeDebugOutput() and report/fatal mirroring.
                bool enableDebugOutput = true;
                /// @brief Enables the logger-owned fatal popup path used by reportFatal(), fatalTerminate(), and
                /// report(..., ReportPopup::Fatal, ...).
                bool enableFatalPopup = true;
                /// @brief Flushes the file stream after every worker batch when true.
                /// @details Safer for tailing/crash diagnostics, but slower under heavy file logging.
                bool flushFileEveryBatch = false;
                /// @brief Flushes stdout/stderr after every console write when true.
                bool flushConsoleEveryWrite = false;
                /// @brief Releases oversized heap fallback text after entries are cleared.
                /// @details False retains peak capacity for better post-spike throughput. lowMemoryConfig() sets this
                /// true.
                bool releaseMessageMemoryAfterWrite = false;
                /// @brief Releases queue, batch, arena, and source-registry storage during shutdown.
                /// @details Lowers idle memory at the cost of reallocating on the next init().
                bool releaseStorageOnShutdown = true;
            };

            /// @brief Effective queue and message limits chosen by init().
            struct QueueLimits
            {
                /// @brief Soft queue depth where low-priority messages may start dropping.
                std::size_t softQueueSize = 0;
                /// @brief Hard queue depth where every severity may drop.
                std::size_t hardQueueSize = 0;
                /// @brief Sanitized hard queue multiplier requested at init(); hardQueueSize is authoritative after
                /// rounding/fallback.
                double hardQueueMultiplier = 1.0;
                /// @brief Maximum stored message length before truncation.
                std::size_t maxMessageLength = 0;
                /// @brief Per-slot preallocated message bytes.
                std::size_t inlineMessageCapacity = 0;
                /// @brief Worker entries drained per batch.
                std::size_t workerBatchSize = 0;
            };

            /// @brief Resettable diagnostic counters visible since init() or resetStats().
            /// @details Queue-drop counters are the only counters that contribute to getLifetimeDroppedLogCount().
            /// Filtered messages are intentional skips and are not counted.
            struct Stats
            {
                /// @brief Messages accepted into the async queue.
                std::size_t queued = 0;
                /// @brief Messages accepted by at least one enabled output sink.
                std::size_t written = 0;
                /// @brief Low-priority messages refused because the soft queue limit was reached.
                std::size_t queueDropsSoft = 0;
                /// @brief Messages refused because the hard queue limit was reached.
                std::size_t queueDropsHard = 0;
                /// @brief Allocation or internal copy/format failures observed by logging paths.
                std::size_t allocationFailures = 0;
                /// @brief File write or flush failures observed while other sinks keep running.
                std::size_t fileWriteFailures = 0;
                /// @brief Written queued entries or synchronous reports that used unregistered SourceId values.
                std::size_t unknownSourceUses = 0;
                /// @brief Runtime format strings that failed validation in std::vformat.
                std::size_t formatFailures = 0;
                /// @brief Messages truncated to Config::maxMessageLength.
                std::size_t truncated = 0;
                /// @brief Highest observed queue depth since init() or resetStats().
                std::size_t peakQueueDepth = 0;
            };

            /// @brief Cold diagnostic snapshot of retained logger memory and process memory.
            /// @note Logger-owned fields report retained capacities, not allocator overhead.
            /// @note Worker-local and thread-local scratch buffers are intentionally excluded.
            struct MemoryStats
            {
                /// @brief Best-effort total of fixed logger state and logger-owned retained capacities.
                std::size_t loggerRetainedBytes = 0;
                /// @brief Retained queue vector storage for ring and worker batch entries.
                std::size_t queueStorageBytes = 0;
                /// @brief Preallocated ring and worker-batch message arena bytes.
                std::size_t messageArenaBytes = 0;
                /// @brief Retained source registry storage for the currently published shared registry snapshot.
                std::size_t sourceRegistryBytes = 0;
                /// @brief Retained heap fallback capacity in idle queue entry source/message text; zero when
                /// unavailable.
                std::size_t entryTextHeapCapacityBytes = 0;
                /// @brief True when entryTextHeapCapacityBytes was inspected without racing producers or the worker.
                bool entryTextHeapCapacityAvailable = false;
                /// @brief Current process working-set bytes reported by the OS.
                std::size_t processWorkingSetBytes = 0;
                /// @brief Current process private bytes reported by the OS.
                std::size_t processPrivateBytes = 0;
                /// @brief True when processWorkingSetBytes and processPrivateBytes were queried successfully.
                bool processMemoryAvailable = false;
            };
        };
        /// @}

    private:
        // Source enum conversion helpers ----------------------------------------------------------

        /// @brief True when Enum can be used as a logger source enum.
        template <typename Enum>
        static constexpr bool isSourceEnum =
            std::is_enum_v<std::remove_cvref_t<Enum>> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::Level> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::Output> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::FormatPolicy> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::Result> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::PlatformErrorSource>;

        /// @brief Converts a source enum to the stored SourceId value.
        /// @param value Source enum value.
        /// @return SourceId representation of value.
        template <typename Enum>
            requires(isSourceEnum<Enum>)
        static constexpr Types::SourceId sourceId(Enum value) noexcept
        {
            using Underlying = std::underlying_type_t<std::remove_cvref_t<Enum>>;
            static_assert(
                std::is_unsigned_v<Underlying>,
                "Logger source enums must use an unsigned underlying type, preferably Logger::Types::SourceId.");
            static_assert(sizeof(Underlying) <= sizeof(Types::SourceId), "Logger source enum values must fit in Logger::Types::SourceId.");
            return static_cast<Types::SourceId>(static_cast<Underlying>(value));
        }

    public:
        // Source and format helpers ---------------------------------------------------------------
        /// @name Source and format helpers
        /// @{

        /// @brief Creates a registered source definition from an unsigned enum value and display name.
        /// @param value Enum value to store as the stable SourceId.
        /// @param name Display name copied during init() and written into log lines.
        /// @return Source definition suitable for Types::Config::sources.
        template <typename Enum>
            requires(isSourceEnum<Enum>)
        static constexpr Types::SourceDefinition defineSource(Enum value, std::string_view name) noexcept
        {
            return Types::SourceDefinition{sourceId(value), name};
        }

        /// @brief Marks a runtime format string as intentional.
        /// @param format Runtime format string passed to std::vformat inside the logger.
        /// @return Runtime format wrapper accepted by formatted logger overloads.
        static constexpr Types::RuntimeFormat runtimeFormat(std::string_view format) noexcept
        {
            return Types::RuntimeFormat{format};
        }

        /// @brief Creates a bounded flush timeout for report and fatal APIs.
        /// @param value Maximum time to wait for queued work and sink flushing.
        /// @return Timeout wrapper accepted by report(), reportError(), reportFatal(), and fatalTerminate().
        static constexpr Types::FlushTimeout flushTimeout(std::chrono::milliseconds value) noexcept
        {
            return Types::FlushTimeout{value};
        }
        /// @}

        // Lifecycle and state queries -------------------------------------------------------------
        /// @name Lifecycle
        /// @{

        /// @brief Starts the async logger with copied source definitions and preallocated queue storage.
        /// @param config Startup configuration. Source and filter spans only need to live through this call.
        /// @return Success on normal startup, or a non-success Types::Result if configuration or setup fell
        /// back/failed.
        /// @note init(), shutdown(), and process-exit cleanup are memory-safe against racing producers; logs submitted
        /// after disabled state is published may be skipped.
        static Types::Result init(const Types::Config &config);
        /// @brief Builds the normal default startup configuration.
        /// @return Types::Config with both console and file output using the default log directory.
        static Types::Config defaultConfig();
        /// @brief Builds a low-retained-memory configuration for tools/tests that prefer small buffers.
        /// @return Types::Config tuned for lower queue and arena memory.
        static Types::Config lowMemoryConfig();
        /// @brief Builds a higher-throughput configuration for heavier logging bursts.
        /// @return Types::Config tuned for larger queues and retained scratch reuse.
        static Types::Config throughputConfig();
        /// @brief Starts the logger with defaultConfig().
        /// @return Types::Result from init(defaultConfig()).
        static Types::Result initDefault();
        /// @brief Starts a console-only logger with a chosen minimum level.
        /// @param minLevel Startup severity floor.
        /// @return Types::Result from init() with Types::Output::Console.
        static Types::Result initConsole(Types::Level minLevel = Types::Level::Info);
        /// @brief Starts a file-only logger with a chosen directory and minimum level.
        /// @param directory UTF-8/narrow log directory path. Empty uses the default directory.
        /// @param minLevel Startup severity floor.
        /// @return Types::Result from init() with Types::Output::File.
        static Types::Result initFile(std::string_view directory = {}, Types::Level minLevel = Types::Level::Info);
        /// @brief Stops the worker, drains queued logs, and closes the file sink.
        /// @note Safe to call while producers are still logging, but shutdown does not guarantee delivery for logs
        /// submitted after disabled state is published.
        static void shutdown();
        /// @brief Waits for accepted queued logs to drain and flushes console/file sinks.
        /// @details This public call is serialized with init() and shutdown().
        /// @note Concurrent producers may enqueue after flush() observes the queue as drained; this is not a
        /// stop-the-world barrier.
        static void flush();
        /// @brief Waits for accepted queued logs to drain and flushes console/file sinks until timeout expires.
        /// @param timeout Maximum duration to wait.
        /// @return True when the queue drained and sinks flushed before timeout expired.
        /// @note Concurrent producers may enqueue after flush(timeout) observes the queue as drained; this is not a
        /// stop-the-world barrier.
        static bool flush(std::chrono::milliseconds timeout);
        /// @}

        /// @name Runtime inspection
        /// @{

        /// @brief Returns true while the worker thread is active and normal logs may be accepted.
        /// @return True after successful init and before shutdown begins.
        static bool isRunning();

        /// @brief Returns the startup severity floor from the active configuration.
        /// @return Current startup minimum level.
        static Types::Level getMinLevel();
        /// @brief Returns the current output mode, including any file-setup fallback to Console.
        /// @return Current output mode.
        static Types::Output getOutput();
        /// @brief Returns the current log file path, or an empty string when file output is unavailable.
        /// @return Current log file path as UTF-8/narrow text.
        static std::string getLogFilePath();
        /// @brief Returns the queue/message limits stored by the logger.
        /// @return Effective limits selected by init(), or default limits before first init().
        static Types::QueueLimits getQueueLimits();

        /// @brief Returns the lifetime count of logs refused because of queue pressure since init().
        /// @return Soft and hard queue-drop total since init().
        static std::size_t getLifetimeDroppedLogCount();
        /// @brief Returns the most recent operation result recorded by the logger.
        /// @return Last logger Types::Result value.
        static Types::Result getLastResult();
        /// @brief Returns the most recent native platform error details, if any.
        /// @return Last platform error, or Types::PlatformErrorSource::None when no platform error is recorded.
        static Types::PlatformError getLastPlatformError();

        /// @brief Returns a relaxed atomic snapshot of visible counters since init or resetStats().
        /// @return Types::Stats snapshot suitable for diagnostics.
        static Types::Stats getStats();
        /// @brief Returns a cold diagnostic memory snapshot without adding hot-path accounting.
        /// @return Best-effort logger-retained memory plus platform process memory when available.
        static Types::MemoryStats getMemoryStats();
        /// @brief Resets visible Types::Stats counters without clearing lifetime queue-drop reporting state.
        static void resetStats();
        /// @}

        // Runtime filters -------------------------------------------------------------------------
        /// @name Runtime filters
        /// @{

        /// @brief Cheap side-effect-free guard for severity-only string source logs.
        /// @param level Severity to test.
        /// @return True when the logger is running, output is enabled, level is at/above minLevel, and the level is not
        /// runtime-filtered.
        static bool shouldLog(Types::Level level);
        /// @brief Cheap side-effect-free guard for string source logs; string sources do not have runtime source
        /// filters.
        /// @param level Severity to test.
        /// @param source String source ignored for filtering; accepted to support lazy LOGGER_* macros uniformly.
        /// @return Same result as shouldLog(level).
        static bool shouldLog(Types::Level level, std::string_view source);
        /// @brief Cheap side-effect-free guard that also checks runtime source filters.
        /// @param level Severity to test.
        /// @param source SourceId to test. Unknown SourceId values are accepted intentionally.
        /// @return True when shouldLog(level) passes and the registered source is enabled or unknown.
        static bool shouldLog(Types::Level level, Types::SourceId source);

        /// @brief Cheap guard for enum source logs that also checks runtime source filters.
        /// @param level Severity to test.
        /// @param source Enum source to test.
        /// @return True when shouldLog(level, SourceId) passes for the enum value.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static bool shouldLog(Types::Level level, Source source)
        {
            return shouldLog(level, sourceId(source));
        }

        /// @brief Enables or disables one registered source at runtime.
        /// @param source Registered SourceId to change.
        /// @param enabled True to allow this source, false to filter it out.
        /// @return Success, or InvalidSourceFilter if the source is not registered.
        static Types::Result setSourceFilter(Types::SourceId source, bool enabled);
        /// @brief Clears one registered source filter by enabling that source.
        /// @param source Registered SourceId to enable.
        /// @return Success, or InvalidSourceFilter if the source is not registered.
        static Types::Result clearSourceFilter(Types::SourceId source);
        /// @brief Clears all registered source filters by enabling every registered source.
        static void clearSourceFilters();

        /// @brief Enables or disables one exact severity level at runtime.
        /// @param level Exact severity level to change.
        /// @param enabled True to allow this level, false to filter it out.
        /// @return Success, or InvalidLevelFilter if the level enum value is invalid.
        static Types::Result setLevelFilter(Types::Level level, bool enabled);
        /// @brief Clears one exact-level filter by enabling that level.
        /// @param level Exact severity level to enable.
        /// @return Success, or InvalidLevelFilter if the level enum value is invalid.
        static Types::Result clearLevelFilter(Types::Level level);
        /// @brief Clears all exact-level filters by enabling every level.
        static void clearLevelFilters();

        /// @brief Enables or disables one enum source at runtime.
        /// @param source Enum source to change.
        /// @param enabled True to allow this source, false to filter it out.
        /// @return Success, or InvalidSourceFilter if the enum source is not registered.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static Types::Result setSourceFilter(Source source, bool enabled)
        {
            return setSourceFilter(sourceId(source), enabled);
        }

        /// @brief Clears one enum source filter by enabling that source.
        /// @param source Enum source to enable.
        /// @return Success, or InvalidSourceFilter if the enum source is not registered.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static Types::Result clearSourceFilter(Source source)
        {
            return clearSourceFilter(sourceId(source));
        }
        /// @}

        // Generic logging overloads ---------------------------------------------------------------
        /// @name Normal logging
        /// @{

        /// @brief Logs a preformatted message with a string source.
        /// @param level Severity for this message.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void log(Types::Level level, std::string_view source, std::string_view message);
        /// @brief Logs a preformatted message with a registered SourceId.
        /// @param level Severity for this message.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void log(Types::Level level, Types::SourceId source, std::string_view message);

        /// @brief Logs a preformatted message with an enum source.
        /// @param level Severity for this message.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void log(Types::Level level, Source source, std::string_view message)
        {
            log(level, sourceId(source), message);
        }

        /// @brief Logs a compile-time checked formatted message with a string source.
        /// @param level Severity for this message.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void log(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(level, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked formatted message with a registered SourceId.
        /// @param level Severity for this message.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void log(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(level, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked formatted message with an enum source.
        /// @param level Severity for this message.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void log(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(level, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs an explicitly runtime formatted message with a string source.
        /// @param level Severity for this message.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void log(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(level, source, format, args...);
        }

        /// @brief Logs an explicitly runtime formatted message with a registered SourceId.
        /// @param level Severity for this message.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void log(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(level, source, format, args...);
        }

        /// @brief Logs an explicitly runtime formatted message with an enum source.
        /// @param level Severity for this message.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void log(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(level, sourceId(source), format, args...);
        }

        // Severity helper overloads ---------------------------------------------------------------

        /// @brief Logs a Trace message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void trace(std::string_view source, std::string_view message);
        /// @brief Logs a Trace message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void trace(Types::SourceId source, std::string_view message);

        /// @brief Logs a Trace message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void trace(Source source, std::string_view message)
        {
            trace(sourceId(source), message);
        }

        /// @brief Logs a Debug message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void debug(std::string_view source, std::string_view message);
        /// @brief Logs a Debug message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void debug(Types::SourceId source, std::string_view message);

        /// @brief Logs a Debug message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void debug(Source source, std::string_view message)
        {
            debug(sourceId(source), message);
        }

        /// @brief Logs an Info message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void info(std::string_view source, std::string_view message);
        /// @brief Logs an Info message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void info(Types::SourceId source, std::string_view message);

        /// @brief Logs an Info message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void info(Source source, std::string_view message)
        {
            info(sourceId(source), message);
        }

        /// @brief Logs a Warn message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void warn(std::string_view source, std::string_view message);
        /// @brief Logs a Warn message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void warn(Types::SourceId source, std::string_view message);

        /// @brief Logs a Warn message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void warn(Source source, std::string_view message)
        {
            warn(sourceId(source), message);
        }

        /// @brief Logs an Error message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void error(std::string_view source, std::string_view message);
        /// @brief Logs an Error message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void error(Types::SourceId source, std::string_view message);

        /// @brief Logs an Error message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void error(Source source, std::string_view message)
        {
            error(sourceId(source), message);
        }

        /// @brief Logs a Fatal message with a string source without forcing a fatal popup.
        /// @param source Source text copied into the queue entry.
        /// @param message Message text copied into the queue entry.
        static void fatal(std::string_view source, std::string_view message);
        /// @brief Logs a Fatal message with a registered SourceId without forcing a fatal popup.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Message text copied into the queue entry.
        static void fatal(Types::SourceId source, std::string_view message);

        /// @brief Logs a Fatal message with an enum source without forcing a fatal popup.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param message Message text copied into the queue entry.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void fatal(Source source, std::string_view message)
        {
            fatal(sourceId(source), message);
        }

        /// @brief Logs a compile-time checked Trace format with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void trace(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Trace, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Trace format with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void trace(Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Trace, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Trace format with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void trace(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Trace, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Debug format with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void debug(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Debug, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Debug format with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void debug(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Debug, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Debug format with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void debug(Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Debug, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Info format with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void info(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Info, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Info format with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void info(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Info, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Info format with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void info(Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Info, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Warn format with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void warn(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Warn, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Warn format with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void warn(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Warn, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Warn format with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void warn(Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Warn, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Error format with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void error(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Error, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Error format with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void error(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Error, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Error format with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void error(Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Error, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Fatal format with a string source without forcing a fatal popup.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void fatal(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Fatal, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Fatal format with a registered SourceId without forcing a fatal popup.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void fatal(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Fatal, source, format, std::forward<Args>(args)...);
        }

        /// @brief Logs a compile-time checked Fatal format with an enum source without forcing a fatal popup.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void fatal(Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndLog(Types::Level::Fatal, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Logs a runtime formatted Trace message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void trace(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Trace, source, format, args...);
        }

        /// @brief Logs a runtime formatted Trace message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void trace(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Trace, source, format, args...);
        }

        /// @brief Logs a runtime formatted Trace message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void trace(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Trace, sourceId(source), format, args...);
        }

        /// @brief Logs a runtime formatted Debug message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void debug(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Debug, source, format, args...);
        }

        /// @brief Logs a runtime formatted Debug message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void debug(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Debug, source, format, args...);
        }

        /// @brief Logs a runtime formatted Debug message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void debug(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Debug, sourceId(source), format, args...);
        }

        /// @brief Logs a runtime formatted Info message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void info(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Info, source, format, args...);
        }

        /// @brief Logs a runtime formatted Info message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void info(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Info, source, format, args...);
        }

        /// @brief Logs a runtime formatted Info message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void info(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Info, sourceId(source), format, args...);
        }

        /// @brief Logs a runtime formatted Warn message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void warn(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Warn, source, format, args...);
        }

        /// @brief Logs a runtime formatted Warn message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void warn(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Warn, source, format, args...);
        }

        /// @brief Logs a runtime formatted Warn message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void warn(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Warn, sourceId(source), format, args...);
        }

        /// @brief Logs a runtime formatted Error message with a string source.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void error(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Error, source, format, args...);
        }

        /// @brief Logs a runtime formatted Error message with a registered SourceId.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void error(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Error, source, format, args...);
        }

        /// @brief Logs a runtime formatted Error message with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void error(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Error, sourceId(source), format, args...);
        }

        /// @brief Logs a runtime formatted Fatal message with a string source without forcing a fatal popup.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void fatal(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Fatal, source, format, args...);
        }

        /// @brief Logs a runtime formatted Fatal message with a registered SourceId without forcing a fatal popup.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void fatal(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Fatal, source, format, args...);
        }

        /// @brief Logs a runtime formatted Fatal message with an enum source without forcing a fatal popup.
        /// @param source Enum source stored as a SourceId in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void fatal(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndLog(Types::Level::Fatal, sourceId(source), format, args...);
        }
        /// @}

        // Reporting and platform debug output overloads ----------------------------------------------------
        /// @name Reports and debug output
        /// @{

        /// @brief Synchronously reports a preformatted diagnostic with a string source and no logger-owned popup.
        /// @details report() bypasses min-level/source filters and the async queue. It writes immediately
        /// to active sinks, mirrors to platform debug output when enabled, flushes, and never terminates.
        /// It does not preserve ordering against older queued async log entries.
        /// @param level Severity to write.
        /// @param source Source text used for the report line.
        /// @param message Message text to write.
        static void report(Types::Level level, std::string_view source, std::string_view message);
        /// @brief Synchronously reports a preformatted diagnostic with a string source and bounded drain/flush.
        /// @return True when the bounded post-report drain/flush completed.
        static bool report(Types::Level level, std::string_view source, Types::FlushTimeout timeout, std::string_view message);
        /// @brief Synchronously reports a preformatted diagnostic with a SourceId and no logger-owned popup.
        static void report(Types::Level level, Types::SourceId source, std::string_view message);
        /// @brief Synchronously reports a preformatted diagnostic with a SourceId and bounded drain/flush.
        /// @return True when the bounded post-report drain/flush completed.
        static bool report(Types::Level level, Types::SourceId source, Types::FlushTimeout timeout, std::string_view message);

        /// @brief Synchronously reports a preformatted diagnostic with explicit popup behavior.
        static void report(Types::Level level, std::string_view source, Types::ReportPopup popup, std::string_view message);
        /// @brief Synchronously reports a preformatted diagnostic with explicit popup behavior and bounded drain/flush.
        /// @return True when the bounded post-report drain/flush completed.
        static bool report(
            Types::Level level,
            std::string_view source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            std::string_view message);
        /// @brief Synchronously reports a preformatted diagnostic with a SourceId and explicit popup behavior.
        static void report(Types::Level level, Types::SourceId source, Types::ReportPopup popup, std::string_view message);
        /// @brief Synchronously reports a preformatted diagnostic with a SourceId, popup behavior, and bounded
        /// drain/flush.
        /// @return True when the bounded post-report drain/flush completed.
        static bool report(
            Types::Level level,
            Types::SourceId source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            std::string_view message);

        /// @brief Synchronously reports a preformatted diagnostic with an enum source.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void report(Types::Level level, Source source, std::string_view message)
        {
            report(level, sourceId(source), message);
        }

        /// @brief Synchronously reports a preformatted diagnostic with an enum source and bounded drain/flush.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static bool report(Types::Level level, Source source, Types::FlushTimeout timeout, std::string_view message)
        {
            return report(level, sourceId(source), timeout, message);
        }

        /// @brief Synchronously reports a preformatted diagnostic with an enum source and explicit popup behavior.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void report(Types::Level level, Source source, Types::ReportPopup popup, std::string_view message)
        {
            report(level, sourceId(source), popup, message);
        }

        /// @brief Synchronously reports a preformatted diagnostic with an enum source, bounded drain/flush, and
        /// explicit popup behavior.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static bool report(Types::Level level, Source source, Types::FlushTimeout timeout, Types::ReportPopup popup, std::string_view message)
        {
            return report(level, sourceId(source), timeout, popup, message);
        }

        /// @brief Synchronously reports an Error diagnostic, mirrors it to platform debug output, and flushes without
        /// showing a fatal popup.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param message Message text written into the report line and platform debug output line.
        static void reportError(std::string_view source, std::string_view message);
        /// @brief Synchronously reports an Error diagnostic, mirrors it to platform debug output, and waits for a
        /// bounded drain/flush.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum time to wait for the flush.
        /// @param message Message text written into the report line and platform debug output line.
        /// @return True when the bounded flush completed.
        static bool reportError(std::string_view source, Types::FlushTimeout timeout, std::string_view message);
        /// @brief Synchronously reports an Error diagnostic with a SourceId, mirrors it to platform debug output, and
        /// flushes without showing a fatal popup.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param message Message text written into the report line and platform debug output line.
        static void reportError(Types::SourceId source, std::string_view message);
        /// @brief Synchronously reports an Error diagnostic with a SourceId, mirrors it to platform debug output, and
        /// waits for a bounded drain/flush.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param message Message text written into the report line and platform debug output line.
        /// @return True when the bounded flush completed.
        static bool reportError(Types::SourceId source, Types::FlushTimeout timeout, std::string_view message);

        /// @brief Synchronously reports an Error diagnostic with an enum source, mirrors it to platform debug output,
        /// and flushes without showing a fatal popup.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param message Message text written into the report line and platform debug output line.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void reportError(Source source, std::string_view message)
        {
            reportError(sourceId(source), message);
        }

        /// @brief Synchronously reports an Error diagnostic with an enum source, mirrors it to platform debug output,
        /// and waits for a bounded drain/flush.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param message Message text written into the report line and platform debug output line.
        /// @return True when the bounded flush completed.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static bool reportError(Source source, Types::FlushTimeout timeout, std::string_view message)
        {
            return reportError(sourceId(source), timeout, message);
        }

        /// @brief Synchronously reports a Fatal diagnostic, mirrors it to platform debug output, flushes, and shows the
        /// fatal popup when enabled.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param message Message text written into the report line, platform debug output line, and fatal popup.
        static void reportFatal(std::string_view source, std::string_view message);
        /// @brief Synchronously reports a Fatal diagnostic, mirrors it to platform debug output, waits for a bounded
        /// drain/flush, and shows the fatal popup when enabled.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum time to wait for the flush.
        /// @param message Message text written into the report line, platform debug output line, and fatal popup.
        /// @return True when the bounded flush completed.
        static bool reportFatal(std::string_view source, Types::FlushTimeout timeout, std::string_view message);
        /// @brief Synchronously reports a Fatal diagnostic with a SourceId, mirrors it to platform debug output,
        /// flushes, and shows the fatal popup when enabled.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param message Message text written into the report line, platform debug output line, and fatal popup.
        static void reportFatal(Types::SourceId source, std::string_view message);
        /// @brief Synchronously reports a Fatal diagnostic with a SourceId, mirrors it to platform debug output, waits
        /// for a bounded drain/flush, and shows the fatal popup when enabled.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param message Message text written into the report line, platform debug output line, and fatal popup.
        /// @return True when the bounded flush completed.
        static bool reportFatal(Types::SourceId source, Types::FlushTimeout timeout, std::string_view message);

        /// @brief Synchronously reports a Fatal diagnostic with an enum source, mirrors it to platform debug output,
        /// flushes, and shows the fatal popup when enabled.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param message Message text written into the report line, platform debug output line, and fatal popup.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static void reportFatal(Source source, std::string_view message)
        {
            reportFatal(sourceId(source), message);
        }

        /// @brief Synchronously reports a Fatal diagnostic with an enum source, mirrors it to platform debug output,
        /// waits for a bounded drain/flush, and shows the fatal popup when enabled.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param message Message text written into the report line, platform debug output line, and fatal popup.
        /// @return True when the bounded flush completed.
        template <typename Source>
            requires(isSourceEnum<Source>)
        static bool reportFatal(Source source, Types::FlushTimeout timeout, std::string_view message)
        {
            return reportFatal(sourceId(source), timeout, message);
        }

        /// @brief Logs fatal, mirrors to platform debug output, flushes, shows the fatal popup when enabled, then
        /// terminates the process.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param message Message text written into the report line and platform debug output line.
        [[noreturn]] static void fatalTerminate(std::string_view source, std::string_view message);
        /// @brief Logs fatal with a SourceId, mirrors to platform debug output, flushes, shows the fatal popup when
        /// enabled, then terminates.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param message Message text written into the report line and platform debug output line.
        [[noreturn]] static void fatalTerminate(Types::SourceId source, std::string_view message);
        /// @brief Logs fatal, waits for a bounded flush, shows the fatal popup when enabled, then terminates.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param message Message text written into the report line and platform debug output line.
        [[noreturn]] static void fatalTerminate(std::string_view source, Types::FlushTimeout timeout, std::string_view message);
        /// @brief Logs fatal with a SourceId, waits for a bounded flush, shows the fatal popup when enabled, then
        /// terminates.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param message Message text written into the report line and platform debug output line.
        [[noreturn]] static void fatalTerminate(Types::SourceId source, Types::FlushTimeout timeout, std::string_view message);

        /// @brief Logs fatal with an enum source, flushes, shows the fatal popup when enabled, then terminates.
        /// @param source Enum source stored as a SourceId.
        /// @param message Message text written into the report line and platform debug output line.
        template <typename Source>
            requires(isSourceEnum<Source>)
        [[noreturn]] static void fatalTerminate(Source source, std::string_view message)
        {
            fatalTerminate(sourceId(source), message);
        }

        /// @brief Logs fatal with an enum source, waits for a bounded flush, shows the fatal popup when enabled, then
        /// terminates.
        /// @param source Enum source stored as a SourceId.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param message Message text written into the report line and platform debug output line.
        template <typename Source>
            requires(isSourceEnum<Source>)
        [[noreturn]] static void fatalTerminate(Source source, Types::FlushTimeout timeout, std::string_view message)
        {
            fatalTerminate(sourceId(source), timeout, message);
        }

        /// @brief Formats and synchronously reports a diagnostic with a string source.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndReport(level, source, false, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a string source and bounded drain/flush.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            std::string_view source,
            Types::FlushTimeout timeout,
            std::format_string<Args...> format,
            Args &&...args)
        {
            return formatAndReport(level, source, false, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a string source and explicit popup behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, std::string_view source, Types::ReportPopup popup, std::format_string<Args...> format, Args &&...args)
        {
            formatAndReport(level, source, popup == Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a string source, bounded drain/flush, and
        /// explicit popup behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            std::string_view source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            std::format_string<Args...> format,
            Args &&...args)
        {
            return formatAndReport(level, source, popup == Types::ReportPopup::Fatal, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a registered SourceId.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndReport(level, source, false, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a registered SourceId and bounded drain/flush.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            Types::SourceId source,
            Types::FlushTimeout timeout,
            std::format_string<Args...> format,
            Args &&...args)
        {
            return formatAndReport(level, source, false, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a registered SourceId and explicit popup
        /// behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, Types::SourceId source, Types::ReportPopup popup, std::format_string<Args...> format, Args &&...args)
        {
            formatAndReport(level, source, popup == Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with a registered SourceId, bounded drain/flush, and
        /// explicit popup behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            Types::SourceId source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            std::format_string<Args...> format,
            Args &&...args)
        {
            return formatAndReport(level, source, popup == Types::ReportPopup::Fatal, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with an enum source.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void report(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args)
        {
            formatAndReport(level, sourceId(source), false, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with an enum source and bounded drain/flush.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool report(Types::Level level, Source source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return formatAndReport(level, sourceId(source), false, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with an enum source and explicit popup behavior.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void report(Types::Level level, Source source, Types::ReportPopup popup, std::format_string<Args...> format, Args &&...args)
        {
            formatAndReport(level, sourceId(source), popup == Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and synchronously reports a diagnostic with an enum source, bounded drain/flush, and explicit
        /// popup behavior.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            Source source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            std::format_string<Args...> format,
            Args &&...args)
        {
            return formatAndReport(level, sourceId(source), popup == Types::ReportPopup::Fatal, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports an error with a string source.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportError(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Error, source, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports an error with a string source and bounded flush.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportError(std::string_view source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return report(Types::Level::Error, source, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports an error with a registered SourceId.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportError(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Error, source, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports an error with a registered SourceId and bounded flush.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportError(Types::SourceId source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return report(Types::Level::Error, source, timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports an error with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void reportError(Source source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Error, sourceId(source), format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports an error with an enum source and bounded flush.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool reportError(Source source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return report(Types::Level::Error, sourceId(source), timeout, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports fatal with a string source.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportFatal(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports fatal with a string source and bounded flush.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportFatal(std::string_view source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports fatal with a registered SourceId.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportFatal(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports fatal with a registered SourceId and bounded flush.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportFatal(Types::SourceId source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports fatal with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void reportFatal(Source source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, sourceId(source), Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and reports fatal with an enum source and bounded flush.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool reportFatal(Source source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            return report(Types::Level::Fatal, sourceId(source), timeout, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a string source.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndReport(level, source, false, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a string source and bounded drain/flush.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(Types::Level level, std::string_view source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return runtimeFormatAndReport(level, source, false, timeout, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a string source and explicit popup
        /// behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, std::string_view source, Types::ReportPopup popup, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndReport(level, source, popup == Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a string source, bounded drain/flush, and
        /// explicit popup behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            std::string_view source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            Types::RuntimeFormat format,
            Args &&...args)
        {
            return runtimeFormatAndReport(level, source, popup == Types::ReportPopup::Fatal, timeout, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a registered SourceId.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndReport(level, source, false, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a registered SourceId and bounded
        /// drain/flush.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(Types::Level level, Types::SourceId source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return runtimeFormatAndReport(level, source, false, timeout, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a registered SourceId and explicit popup
        /// behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void report(Types::Level level, Types::SourceId source, Types::ReportPopup popup, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndReport(level, source, popup == Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with a registered SourceId, bounded
        /// drain/flush, and explicit popup behavior.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            Types::SourceId source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            Types::RuntimeFormat format,
            Args &&...args)
        {
            return runtimeFormatAndReport(level, source, popup == Types::ReportPopup::Fatal, timeout, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with an enum source.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void report(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndReport(level, sourceId(source), false, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with an enum source and bounded drain/flush.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool report(Types::Level level, Source source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return runtimeFormatAndReport(level, sourceId(source), false, timeout, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with an enum source and explicit popup
        /// behavior.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void report(Types::Level level, Source source, Types::ReportPopup popup, Types::RuntimeFormat format, Args &&...args)
        {
            runtimeFormatAndReport(level, sourceId(source), popup == Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and synchronously reports a diagnostic with an enum source, bounded drain/flush, and
        /// explicit popup behavior.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool report(
            Types::Level level,
            Source source,
            Types::FlushTimeout timeout,
            Types::ReportPopup popup,
            Types::RuntimeFormat format,
            Args &&...args)
        {
            return runtimeFormatAndReport(level, sourceId(source), popup == Types::ReportPopup::Fatal, timeout, format, args...);
        }

        /// @brief Runtime-formats and reports an error with a string source.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportError(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Error, source, format, args...);
        }

        /// @brief Runtime-formats and reports an error with a string source and bounded flush.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportError(std::string_view source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return report(Types::Level::Error, source, timeout, format, args...);
        }

        /// @brief Runtime-formats and reports an error with a registered SourceId.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportError(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Error, source, format, args...);
        }

        /// @brief Runtime-formats and reports an error with a registered SourceId and bounded flush.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportError(Types::SourceId source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return report(Types::Level::Error, source, timeout, format, args...);
        }

        /// @brief Runtime-formats and reports an error with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void reportError(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Error, sourceId(source), format, args...);
        }

        /// @brief Runtime-formats and reports an error with an enum source and bounded flush.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool reportError(Source source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return report(Types::Level::Error, sourceId(source), timeout, format, args...);
        }

        /// @brief Runtime-formats and reports fatal with a string source.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportFatal(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and reports fatal with a string source and bounded flush.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportFatal(std::string_view source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and reports fatal with a registered SourceId.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static void reportFatal(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and reports fatal with a registered SourceId and bounded flush.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        static bool reportFatal(Types::SourceId source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and reports fatal with an enum source.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static void reportFatal(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, sourceId(source), Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Runtime-formats and reports fatal with an enum source and bounded flush.
        /// @param source Enum source stored as a SourceId in the queue entry and resolved for platform debug output.
        /// @param timeout Maximum time to wait for the flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        /// @return True when the bounded flush completed.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        static bool reportFatal(Source source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            return report(Types::Level::Fatal, sourceId(source), timeout, Types::ReportPopup::Fatal, format, args...);
        }

        /// @brief Formats fatal, flushes, shows the fatal popup when enabled, then terminates.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
            std::terminate();
        }

        /// @brief Formats fatal with a SourceId, flushes, shows the fatal popup when enabled, then terminates.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
            std::terminate();
        }

        /// @brief Formats fatal with an enum source, flushes, shows the fatal popup when enabled, then terminates.
        /// @param source Enum source stored as a SourceId.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Source source, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, sourceId(source), Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
            std::terminate();
        }

        /// @brief Formats fatal, waits for a bounded flush, shows the fatal popup when enabled, then terminates.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(
            std::string_view source,
            Types::FlushTimeout timeout,
            std::format_string<Args...> format,
            Args &&...args)
        {
            report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
            std::terminate();
        }

        /// @brief Formats fatal with a SourceId, waits for a bounded flush, shows the fatal popup when enabled, then
        /// terminates.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(
            Types::SourceId source,
            Types::FlushTimeout timeout,
            std::format_string<Args...> format,
            Args &&...args)
        {
            report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
            std::terminate();
        }

        /// @brief Formats fatal with an enum source, waits for a bounded flush, shows the fatal popup when enabled,
        /// then terminates.
        /// @param source Enum source stored as a SourceId.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Source source, Types::FlushTimeout timeout, std::format_string<Args...> format, Args &&...args)
        {
            report(Types::Level::Fatal, sourceId(source), timeout, Types::ReportPopup::Fatal, format, std::forward<Args>(args)...);
            std::terminate();
        }

        /// @brief Runtime-formats fatal, flushes, shows the fatal popup when enabled, then terminates.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(std::string_view source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, args...);
            std::terminate();
        }

        /// @brief Runtime-formats fatal with a SourceId, flushes, shows the fatal popup when enabled, then terminates.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, source, Types::ReportPopup::Fatal, format, args...);
            std::terminate();
        }

        /// @brief Runtime-formats fatal with an enum source, flushes, shows the fatal popup when enabled, then
        /// terminates.
        /// @param source Enum source stored as a SourceId.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Source source, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, sourceId(source), Types::ReportPopup::Fatal, format, args...);
            std::terminate();
        }

        /// @brief Runtime-formats fatal, waits for a bounded flush, shows the fatal popup when enabled, then
        /// terminates.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(std::string_view source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, args...);
            std::terminate();
        }

        /// @brief Runtime-formats fatal with a SourceId, waits for a bounded flush, shows the fatal popup when enabled,
        /// then terminates.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Types::SourceId source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, source, timeout, Types::ReportPopup::Fatal, format, args...);
            std::terminate();
        }

        /// @brief Runtime-formats fatal with an enum source, waits for a bounded flush, shows the fatal popup when
        /// enabled, then terminates.
        /// @param source Enum source stored as a SourceId.
        /// @param timeout Maximum flush wait before termination continues.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename Source, typename... Args>
            requires(isSourceEnum<Source> && sizeof...(Args) > 0)
        [[noreturn]] static void fatalTerminate(Source source, Types::FlushTimeout timeout, Types::RuntimeFormat format, Args &&...args)
        {
            report(Types::Level::Fatal, sourceId(source), timeout, Types::ReportPopup::Fatal, format, args...);
            std::terminate();
        }

        /// @brief Writes one formatted line directly to the platform debug output when Types::Config::enableDebugOutput
        /// is true.
        /// @note This bypasses the async queue and does not write to console or file sinks.
        /// @param level Severity label to write.
        /// @param source Source text to write.
        /// @param message Message text to write.
        static void writeDebugOutput(Types::Level level, std::string_view source, std::string_view message);
        /// @}

    private:
        // Template implementation helpers ---------------------------------------------------------

        /// @brief Enqueues a preformatted message after the caller's fast-path filter check.
        /// @param level Entry severity.
        /// @param source Source text copied into the queue entry.
        /// @param message Formatted message copied before this call returns.
        static void enqueuePreformattedMessage(Types::Level level, std::string_view source, std::string_view message);
        /// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
        /// @param level Entry severity.
        /// @param source Source text copied into the queue entry.
        /// @param message Formatted message copied before this call returns.
        /// @param alreadyTruncated True when message already includes the truncation suffix.
        static void enqueuePreformattedMessage(Types::Level level, std::string_view source, std::string_view message, bool alreadyTruncated);
        /// @brief Enqueues a preformatted message after the caller's fast-path filter check.
        /// @param level Entry severity.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Formatted message copied before this call returns.
        static void enqueuePreformattedMessage(Types::Level level, Types::SourceId source, std::string_view message);
        /// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
        /// @param level Entry severity.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param message Formatted message copied before this call returns.
        /// @param alreadyTruncated True when message already includes the truncation suffix.
        static void enqueuePreformattedMessage(Types::Level level, Types::SourceId source, std::string_view message, bool alreadyTruncated);
        /// @brief Synchronously reports a preformatted message with a string source.
        /// @param level Entry severity.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param message Formatted message copied before this call returns.
        /// @param showPopup True to show the fatal popup after flush.
        static void reportPreformattedMessage(Types::Level level, std::string_view source, std::string_view message, bool showPopup);
        /// @brief Synchronously reports a preformatted message with a string source and optional bounded drain/flush.
        /// @param level Entry severity.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param message Formatted message copied before this call returns.
        /// @param showPopup True to show the fatal popup after flush.
        /// @param alreadyTruncated True when message already includes the truncation suffix.
        /// @param timeout Optional bounded flush duration.
        /// @return True when the post-report drain/flush completed; blocking reports always return true.
        static bool reportPreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            Types::FlushTimeout *timeout);
        /// @brief Synchronously reports a preformatted message with a registered SourceId.
        /// @param level Entry severity.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param message Formatted message copied before this call returns.
        /// @param showPopup True to show the fatal popup after flush.
        static void reportPreformattedMessage(Types::Level level, Types::SourceId source, std::string_view message, bool showPopup);
        /// @brief Synchronously reports a preformatted message with a registered SourceId and optional bounded
        /// drain/flush.
        /// @param level Entry severity.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param message Formatted message copied before this call returns.
        /// @param showPopup True to show the fatal popup after flush.
        /// @param alreadyTruncated True when message already includes the truncation suffix.
        /// @param timeout Optional bounded flush duration.
        /// @return True when the post-report drain/flush completed; blocking reports always return true.
        static bool reportPreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            Types::FlushTimeout *timeout);
        /// @brief Counts an allocation or internal formatting failure.
        static void recordAllocationFailure();
        /// @brief Counts an invalid runtime formatting failure.
        static void recordFormatFailure();
        /// @brief Returns per-thread formatting scratch storage reused by formatted overloads.
        /// @return Mutable per-thread scratch string.
        static std::string &formatScratch();
        /// @brief Returns the active maximum message length used by bounded formatting.
        /// @return Current maximum message length, or the startup default before init.
        static std::size_t getMaxMessageLengthForFormatting();
        /// @brief Returns the active formatted-message memory/speed policy.
        /// @return Current format policy, or StrictBounded before init.
        static Types::FormatPolicy getFormatPolicyForFormatting();
        /// @brief Releases thread-local format scratch capacity when configured.
        /// @param scratch Scratch buffer to optionally shrink.
        static void releaseFormatScratchIfNeeded(std::string &scratch);

        /// @brief Types::Output iterator that stores only the prefix that can fit before the truncation suffix.
        class BoundedFormatIterator
        {
        public:
            using difference_type = std::ptrdiff_t;
            using value_type = char;
            using pointer = void;
            using reference = void;
            using iterator_category = std::output_iterator_tag;

            BoundedFormatIterator(std::string &output, std::size_t prefixLimit, std::size_t &writtenCount, bool &truncatedFlag) noexcept
                : outputText(&output)
                , limit(prefixLimit)
                , written(&writtenCount)
                , truncated(&truncatedFlag)
            {
            }

            BoundedFormatIterator &operator=(char value)
            {
                if (*written < limit)
                {
                    outputText->push_back(value);
                }
                else
                {
                    *truncated = true;
                }
                ++(*written);
                return *this;
            }

            BoundedFormatIterator &operator*() noexcept
            {
                return *this;
            }
            BoundedFormatIterator &operator++() noexcept
            {
                return *this;
            }
            BoundedFormatIterator operator++(int) noexcept
            {
                return *this;
            }

            bool wasTruncated() const noexcept
            {
                return *truncated;
            }

        private:
            std::string *outputText = nullptr;
            std::size_t limit = 0;
            std::size_t *written = nullptr;
            bool *truncated = nullptr;
        };

        /// @brief Appends the standard truncation suffix after bounded formatting overflows.
        /// @param scratch Scratch buffer containing the retained prefix.
        /// @param maxMessageLength Maximum stored message bytes.
        static void appendTruncationSuffix(std::string &scratch, std::size_t maxMessageLength)
        {
            constexpr std::string_view suffix = "... [truncated]";
            if (maxMessageLength == 0)
            {
                scratch.clear();
                return;
            }
            if (maxMessageLength <= suffix.size())
            {
                scratch.assign(suffix.substr(0, maxMessageLength));
                return;
            }

            scratch.append(suffix);
        }

        /// @brief Truncates scratch in place using the standard logger suffix.
        /// @return True when scratch exceeded maxMessageLength and was truncated.
        static bool truncateScratchIfNeeded(std::string &scratch, std::size_t maxMessageLength)
        {
            constexpr std::string_view suffix = "... [truncated]";
            if (scratch.size() <= maxMessageLength)
            {
                return false;
            }

            if (maxMessageLength > suffix.size())
            {
                scratch.resize(maxMessageLength - suffix.size());
            }
            appendTruncationSuffix(scratch, maxMessageLength);
            return true;
        }

        /// @brief Formats into scratch while retaining at most maxMessageLength bytes.
        /// @return True when output exceeded maxMessageLength and was suffixed.
        template <typename Format, typename... Args>
        static bool formatBounded(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            constexpr std::string_view suffix = "... [truncated]";
            scratch.clear();
            const std::size_t prefixLimit = maxMessageLength;
            std::size_t written = 0;
            bool truncated = false;
            BoundedFormatIterator output(scratch, prefixLimit, written, truncated);
            std::format_to(output, format, std::forward<Args>(args)...);
            if (!truncated)
            {
                return false;
            }

            if (maxMessageLength > suffix.size())
            {
                scratch.resize(maxMessageLength - suffix.size());
            }
            appendTruncationSuffix(scratch, maxMessageLength);
            return true;
        }

        /// @brief Runtime-formats into scratch while retaining at most maxMessageLength bytes.
        /// @return True when output exceeded maxMessageLength and was suffixed.
        template <typename... Args>
        static bool runtimeFormatBounded(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            constexpr std::string_view suffix = "... [truncated]";
            scratch.clear();
            const std::size_t prefixLimit = maxMessageLength;
            std::size_t written = 0;
            bool truncated = false;
            BoundedFormatIterator output(scratch, prefixLimit, written, truncated);
            std::vformat_to(output, format.text, std::make_format_args(args...));
            if (!truncated)
            {
                return false;
            }

            if (maxMessageLength > suffix.size())
            {
                scratch.resize(maxMessageLength - suffix.size());
            }
            appendTruncationSuffix(scratch, maxMessageLength);
            return true;
        }

        /// @brief Formats into scratch normally, then truncates if needed.
        /// @return True when output exceeded maxMessageLength and was suffixed.
        template <typename Format, typename... Args>
        static bool formatFastNormal(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            scratch.clear();
            std::format_to(std::back_inserter(scratch), format, std::forward<Args>(args)...);
            return truncateScratchIfNeeded(scratch, maxMessageLength);
        }

        /// @brief Runtime-formats into scratch normally, then truncates if needed.
        /// @return True when output exceeded maxMessageLength and was suffixed.
        template <typename... Args>
        static bool runtimeFormatFastNormal(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            scratch.clear();
            std::vformat_to(std::back_inserter(scratch), format.text, std::make_format_args(args...));
            return truncateScratchIfNeeded(scratch, maxMessageLength);
        }

        /// @brief Formats using the active memory/speed policy.
        /// @return True when output exceeded maxMessageLength and was suffixed.
        template <typename Format, typename... Args>
        static bool formatWithPolicy(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            if (getFormatPolicyForFormatting() == Types::FormatPolicy::FastNormal)
            {
                return formatFastNormal(scratch, maxMessageLength, format, std::forward<Args>(args)...);
            }
            return formatBounded(scratch, maxMessageLength, format, std::forward<Args>(args)...);
        }

        /// @brief Runtime-formats using the active memory/speed policy.
        /// @return True when output exceeded maxMessageLength and was suffixed.
        template <typename... Args>
        static bool runtimeFormatWithPolicy(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            if (getFormatPolicyForFormatting() == Types::FormatPolicy::FastNormal)
            {
                return runtimeFormatFastNormal(scratch, maxMessageLength, format, args...);
            }
            return runtimeFormatBounded(scratch, maxMessageLength, format, args...);
        }

        /// @brief Formats and enqueues after a string source caller already passed the fast filter guard.
        template <typename... Args>
        static void formatAndEnqueueAfterFilter(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Formats and enqueues after a registered source caller already passed the fast filter guard.
        template <typename... Args>
        static void formatAndEnqueueAfterFilter(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Runtime-formats and enqueues after a string source caller already passed the fast filter guard.
        template <typename... Args>
        static void runtimeFormatAndEnqueueAfterFilter(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Runtime-formats and enqueues after a registered source caller already passed the fast filter guard.
        template <typename... Args>
        static void runtimeFormatAndEnqueueAfterFilter(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Formats and logs a compile-time checked message with a string source.
        /// @param level Entry severity.
        /// @param source Source text copied into the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
        static void formatAndLog(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args)
        {
            if (!shouldLog(level))
            {
                return;
            }

            formatAndEnqueueAfterFilter(level, source, format, std::forward<Args>(args)...);
        }

        /// @brief Formats and logs a compile-time checked message with a registered SourceId.
        /// @param level Entry severity.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
        static void formatAndLog(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args)
        {
            if (!shouldLog(level, source))
            {
                return;
            }

            formatAndEnqueueAfterFilter(level, source, format, std::forward<Args>(args)...);
        }

        /// @brief Runtime-formats and logs a message with a string source.
        /// @param level Entry severity.
        /// @param source Source text copied into the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
        static void runtimeFormatAndLog(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &...args)
        {
            if (!shouldLog(level))
            {
                return;
            }

            runtimeFormatAndEnqueueAfterFilter(level, source, format, args...);
        }

        /// @brief Runtime-formats and logs a message with a registered SourceId.
        /// @param level Entry severity.
        /// @param source Registered SourceId stored in the queue entry.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
        static void runtimeFormatAndLog(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &...args)
        {
            if (!shouldLog(level, source))
            {
                return;
            }

            runtimeFormatAndEnqueueAfterFilter(level, source, format, args...);
        }

        /// @brief Formats and reports a compile-time checked message with a string source.
        /// @param level Entry severity.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param showPopup True to show the fatal popup after flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
        static void formatAndReport(Types::Level level, std::string_view source, bool showPopup, std::format_string<Args...> format, Args &&...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                reportPreformattedMessage(level, source, scratch, showPopup, truncated, nullptr);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Formats and reports a compile-time checked message with a registered SourceId.
        /// @param level Entry severity.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param showPopup True to show the fatal popup after flush.
        /// @param format Compile-time checked format string.
        /// @param args Format arguments.
        template <typename... Args>
        static void formatAndReport(Types::Level level, Types::SourceId source, bool showPopup, std::format_string<Args...> format, Args &&...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                reportPreformattedMessage(level, source, scratch, showPopup, truncated, nullptr);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Formats and reports a compile-time checked message with a string source and bounded flush.
        /// @return True when the bounded flush completed.
        template <typename... Args>
        static bool formatAndReport(
            Types::Level level,
            std::string_view source,
            bool showPopup,
            Types::FlushTimeout timeout,
            std::format_string<Args...> format,
            Args &&...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                const bool flushed = reportPreformattedMessage(level, source, scratch, showPopup, truncated, &timeout);
                releaseFormatScratchIfNeeded(scratch);
                return flushed;
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
            return false;
        }

        /// @brief Formats and reports a compile-time checked message with a registered SourceId and bounded flush.
        /// @return True when the bounded flush completed.
        template <typename... Args>
        static bool formatAndReport(
            Types::Level level,
            Types::SourceId source,
            bool showPopup,
            Types::FlushTimeout timeout,
            std::format_string<Args...> format,
            Args &&...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                const bool flushed = reportPreformattedMessage(level, source, scratch, showPopup, truncated, &timeout);
                releaseFormatScratchIfNeeded(scratch);
                return flushed;
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
            return false;
        }

        /// @brief Runtime-formats and reports a message with a string source.
        /// @param level Entry severity.
        /// @param source Source text written into the report line and platform debug output line.
        /// @param showPopup True to show the fatal popup after flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
        static void runtimeFormatAndReport(Types::Level level, std::string_view source, bool showPopup, Types::RuntimeFormat format, Args &...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                reportPreformattedMessage(level, source, scratch, showPopup, truncated, nullptr);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Runtime-formats and reports a message with a registered SourceId.
        /// @param level Entry severity.
        /// @param source Registered SourceId resolved for the report line and platform debug output.
        /// @param showPopup True to show the fatal popup after flush.
        /// @param format Runtime format wrapper created by Logger::runtimeFormat().
        /// @param args Format arguments.
        template <typename... Args>
        static void runtimeFormatAndReport(Types::Level level, Types::SourceId source, bool showPopup, Types::RuntimeFormat format, Args &...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                reportPreformattedMessage(level, source, scratch, showPopup, truncated, nullptr);
                releaseFormatScratchIfNeeded(scratch);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        /// @brief Runtime-formats and reports a message with a string source and bounded flush.
        /// @return True when the bounded flush completed.
        template <typename... Args>
        static bool runtimeFormatAndReport(
            Types::Level level,
            std::string_view source,
            bool showPopup,
            Types::FlushTimeout timeout,
            Types::RuntimeFormat format,
            Args &...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                const bool flushed = reportPreformattedMessage(level, source, scratch, showPopup, truncated, &timeout);
                releaseFormatScratchIfNeeded(scratch);
                return flushed;
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
            return false;
        }

        /// @brief Runtime-formats and reports a message with a registered SourceId and bounded flush.
        /// @return True when the bounded flush completed.
        template <typename... Args>
        static bool runtimeFormatAndReport(
            Types::Level level,
            Types::SourceId source,
            bool showPopup,
            Types::FlushTimeout timeout,
            Types::RuntimeFormat format,
            Args &...args)
        {
            try
            {
                std::string &scratch = formatScratch();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                const bool flushed = reportPreformattedMessage(level, source, scratch, showPopup, truncated, &timeout);
                releaseFormatScratchIfNeeded(scratch);
                return flushed;
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
            return false;
        }
    };
} // namespace GameWIP
