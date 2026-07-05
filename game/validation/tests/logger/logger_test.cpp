/// @file logger_test.cpp
/// @brief Executable self-tests for the Logger library.

#include "validation/tests/logger/logger_test.h"

#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "terminal/terminal.h"
#include "test_support/test_support.h"

#ifndef INTERNAL_LOGGER_TEST_HOOKS
#define INTERNAL_LOGGER_TEST_HOOKS 0
#endif

#if INTERNAL_LOGGER_TEST_HOOKS
#include "logger/internal/logger_test_hooks.h"
#endif

#ifndef INTERNAL_TERMINAL_TEST_HOOKS
#define INTERNAL_TERMINAL_TEST_HOOKS 0
#endif

#if INTERNAL_TERMINAL_TEST_HOOKS
#include "terminal/internal/terminal_test_hooks.h"
#endif

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/// @brief Marker value whose formatter emits a nested Logger message.
struct LoggerReentrantFormat
{
};

template <> struct std::formatter<LoggerReentrantFormat>
{
    /// @brief Accepts the empty formatter specification used by the reentry fixture.
    constexpr auto parse(std::format_parse_context &context)
    {
        return context.begin();
    }

    /// @brief Queues a nested message before producing the outer formatted value.
    template <typename FormatContext> auto format(const LoggerReentrantFormat &, FormatContext &context) const
    {
        GameWIP::Logger::info("LoggerFormatter", "nested {}", 7);
        return std::format_to(context.out(), "outer");
    }
};

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Terminal = GameWIP::Terminal;
    namespace TestSupport = GameWIP::TestSupport;
    using LoggerTestOptions = GameWIP::Test::LoggerTestOptions;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::string_view testSource = "LoggerTest";
    constexpr std::string_view shortMessage = "logger test message";
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "INTERNAL_LOGGER_TEST_CHILD_LOG_DIR";
    constexpr std::string_view fatalTerminateChildMessage = "child fatal terminate";

    /// @brief Stable source identifiers used by source registration and filtering tests.
    enum class TestSource : Logger::Types::SourceId
    {
        Core = 1,
        Render = 2,
        Audio = 3,
        Unknown = 99
    };

    /// @brief Highest observed memory snapshot and the scenario that produced it.
    struct MemoryPeak
    {
        Logger::Types::MemoryStats memory;
        std::string label;
        bool available = false;
    };

    /// @brief Mutable Logger-suite state with TestSupport-backed reporting and isolated log ownership.
    struct TestContext
    {
        /// @brief Binds this adapter to one TestSupport suite context.
        explicit TestContext(TestSupport::Context &testContext) noexcept
            : testContext(testContext)
        {
        }

        TestSupport::Context &testContext;
        std::filesystem::path logRoot;
        std::string executablePath;
        MemoryPeak loggerMemoryPeak;
        MemoryPeak processMemoryPeak;

        /// @brief Returns the current TestSupport summary snapshot.
        [[nodiscard]] TestSupport::Types::Summary result() const noexcept
        {
            return testContext.result();
        }

        /// @brief Returns whether no failure has been recorded.
        [[nodiscard]] bool ok() const noexcept
        {
            return testContext.ok();
        }

        /// @brief Routes a legacy categorized line through structured TestSupport output.
        void emit(std::string_view line)
        {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            {
                text.pop_back();
            }

            constexpr std::array categories{
                std::pair{std::string_view{"[INFO] "}, &TestSupport::Context::info},
                std::pair{std::string_view{"[MANUAL] "}, &TestSupport::Context::manual},
                std::pair{std::string_view{"[METRIC] "}, &TestSupport::Context::metric},
                std::pair{std::string_view{"[STRESS] "}, &TestSupport::Context::stress},
                std::pair{std::string_view{"[SUMMARY] "}, &TestSupport::Context::summary},
            };

            for (const auto &[prefix, writer] : categories)
            {
                if (text.starts_with(prefix))
                {
                    (testContext.*writer)(std::string_view(text).substr(prefix.size()));
                    return;
                }
            }

            if (text.starts_with("[RESULT] "))
            {
                testContext.summary(std::string_view(text).substr(std::string_view{"[RESULT] "}.size()));
                return;
            }

            testContext.info(text);
        }

        /// @brief Records one passing Logger scenario.
        void pass(std::string_view name)
        {
            testContext.pass(name);
        }

        /// @brief Records one failed Logger scenario with diagnostic details.
        void fail(std::string_view name, std::string_view details)
        {
            testContext.fail(name, details);
        }

        /// @brief Records equality through the shared TestSupport expectation.
        template <typename Left, typename Right> void expectEq(std::string_view name, const Left &actual, const Right &expected)
        {
            static_cast<void>(testContext.expectEq(name, expected, actual));
        }

        /// @brief Records a required true predicate with custom failure details.
        void expectTrue(std::string_view name, bool value, std::string_view details = "expected true")
        {
            if (value)
            {
                testContext.pass(name);
                return;
            }

            testContext.fail(name, details);
        }

        /// @brief Records a required false predicate with custom failure details.
        void expectFalse(std::string_view name, bool value, std::string_view details = "expected false")
        {
            if (!value)
            {
                testContext.pass(name);
                return;
            }

            testContext.fail(name, details);
        }

        /// @brief Records whether text contains the required substring.
        void expectContains(std::string_view name, std::string_view text, std::string_view expectedSubstring)
        {
            static_cast<void>(testContext.expectContains(name, text, expectedSubstring));
        }

        /// @brief Records whether one log file contains the required substring.
        void expectFileContains(std::string_view name, const std::filesystem::path &path, std::string_view expectedSubstring)
        {
            static_cast<void>(testContext.expectFileContains(name, path, expectedSubstring));
        }

        /// @brief Records an exact non-overlapping occurrence count in one log file.
        void expectFileOccurrenceCount(std::string_view name, const std::filesystem::path &path, std::string_view text, std::size_t expectedCount)
        {
            static_cast<void>(testContext.expectFileOccurrenceCount(name, path, text, expectedCount));
        }
    };

    /// @brief Runs one scenario with timing and converts uncaught exceptions into test failures.
    template <typename Function> void runCase(TestContext &context, std::string_view name, Function &&function)
    {
        TestSupport::Section section(context.testContext, name);
        try
        {
            std::forward<Function>(function)();
        }
        catch (const std::exception &exception)
        {
            Logger::shutdown();
            context.fail(name, exception.what());
        }
        catch (...)
        {
            Logger::shutdown();
            context.fail(name, "unknown exception");
        }
    }

    /// @brief Guarantees process-wide Logger shutdown when a scenario scope exits.
    struct ScopedLoggerShutdown
    {
        ~ScopedLoggerShutdown()
        {
            Logger::shutdown();
        }
    };

    /// @brief Returns stable diagnostic text for every Logger result value.
    std::string_view toString(Logger::Types::Result result)
    {
        switch (result)
        {
        case Logger::Types::Result::Success:
            return "Success";
        case Logger::Types::Result::AlreadyRunning:
            return "AlreadyRunning";
        case Logger::Types::Result::InvalidOutputMode:
            return "InvalidOutputMode";
        case Logger::Types::Result::InvalidQueueSize:
            return "InvalidQueueSize";
        case Logger::Types::Result::InvalidMessageLength:
            return "InvalidMessageLength";
        case Logger::Types::Result::InvalidLogDirectory:
            return "InvalidLogDirectory";
        case Logger::Types::Result::InvalidSourceDefinition:
            return "InvalidSourceDefinition";
        case Logger::Types::Result::InvalidSourceFilter:
            return "InvalidSourceFilter";
        case Logger::Types::Result::InvalidLevelFilter:
            return "InvalidLevelFilter";
        case Logger::Types::Result::FileOpenFailed:
            return "FileOpenFailed";
        case Logger::Types::Result::FileWriteFailed:
            return "FileWriteFailed";
        case Logger::Types::Result::FileSetupFailed:
            return "FileSetupFailed";
        case Logger::Types::Result::ThreadStartFailed:
            return "ThreadStartFailed";
        case Logger::Types::Result::PlatformCallFailed:
            return "PlatformCallFailed";
        }

        return "Unknown";
    }

    /// @brief Returns stable diagnostic text for every Logger output mode.
    std::string_view toString(Logger::Types::Output output)
    {
        switch (output)
        {
        case Logger::Types::Output::None:
            return "None";
        case Logger::Types::Output::Console:
            return "Console";
        case Logger::Types::Output::File:
            return "File";
        case Logger::Types::Output::Both:
            return "Both";
        }

        return "Unknown";
    }

    /// @brief Returns stable diagnostic text for every Logger severity.
    std::string_view toString(Logger::Types::Level level)
    {
        switch (level)
        {
        case Logger::Types::Level::Trace:
            return "Trace";
        case Logger::Types::Level::Debug:
            return "Debug";
        case Logger::Types::Level::Info:
            return "Info";
        case Logger::Types::Level::Warn:
            return "Warn";
        case Logger::Types::Level::Error:
            return "Error";
        case Logger::Types::Level::Fatal:
            return "Fatal";
        }

        return "Unknown";
    }

    /// @brief Formats Logger enums and ordinary values for expectation failures.
    template <typename Value> std::string printable(Value value)
    {
        if constexpr (
            std::is_same_v<Value, Logger::Types::Result> || std::is_same_v<Value, Logger::Types::Output> ||
            std::is_same_v<Value, Logger::Types::Level>)
        {
            return std::string(toString(value));
        }
        else if constexpr (std::is_same_v<Value, bool>)
        {
            return value ? "true" : "false";
        }
        else
        {
            return std::format("{}", value);
        }
    }

    /// @brief Records an equality result with readable expected and actual values.
    template <typename Left, typename Right> void expectEq(TestContext &context, std::string_view name, const Left &actual, const Right &expected)
    {
        if (actual == expected)
        {
            context.pass(name);
            return;
        }

        context.fail(name, std::format("expected {}, got {}", printable(expected), printable(actual)));
    }

    /// @brief Formats a byte count with binary units for retained-memory diagnostics.
    std::string formatBytes(std::size_t bytes)
    {
        constexpr std::array<std::string_view, 5> units{"B", "KiB", "MiB", "GiB", "TiB"};
        double value = static_cast<double>(bytes);
        std::size_t unitIndex = 0;
        while (value >= 1024.0 && unitIndex + 1 < units.size())
        {
            value /= 1024.0;
            ++unitIndex;
        }

        if (unitIndex == 0)
        {
            return std::format("{} {}", bytes, units[unitIndex]);
        }

        return std::format("{:.2f} {}", value, units[unitIndex]);
    }

    /// @brief Formats a process-memory value or reports unavailable platform data.
    std::string formatProcessBytes(const Logger::Types::MemoryStats &memory, std::size_t bytes)
    {
        return memory.processMemoryAvailable ? formatBytes(bytes) : "n/a";
    }

    /// @brief Samples Logger/process memory and updates suite peak observations.
    Logger::Types::MemoryStats recordMemorySnapshot(TestContext &context, std::string_view label)
    {
        const Logger::Types::MemoryStats memory = Logger::getMemoryStats();
        if (!context.loggerMemoryPeak.available || memory.loggerRetainedBytes > context.loggerMemoryPeak.memory.loggerRetainedBytes)
        {
            context.loggerMemoryPeak.memory = memory;
            context.loggerMemoryPeak.label = std::string(label);
            context.loggerMemoryPeak.available = true;
        }

        if (memory.processMemoryAvailable &&
            (!context.processMemoryPeak.available || memory.processPrivateBytes > context.processMemoryPeak.memory.processPrivateBytes))
        {
            context.processMemoryPeak.memory = memory;
            context.processMemoryPeak.label = std::string(label);
            context.processMemoryPeak.available = true;
        }

        return memory;
    }

    /// @brief Combines hard and soft queue-pressure drop counters.
    std::size_t totalQueueDrops(const Logger::Types::Stats &stats)
    {
        return stats.queueDropsHard + stats.queueDropsSoft;
    }

    /// @brief Combines allocation, format, and sink diagnostic-failure counters.
    std::size_t totalDiagnosticFailures(const Logger::Types::Stats &stats)
    {
        return stats.allocationFailures + stats.formatFailures + stats.fileWriteFailures;
    }

    /// @brief Converts a fixture path to Logger's narrow configuration text.
    std::string pathText(const std::filesystem::path &path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    }

    /// @brief Converts Logger's UTF-8 path text back to a platform path for test inspection.
    std::filesystem::path pathFromText(std::string_view text)
    {
        const auto *begin = reinterpret_cast<const char8_t *>(text.data());
        return std::filesystem::path(std::u8string(begin, begin + text.size()));
    }

    using ScopedEnvironmentVariable = TestSupport::ScopedEnvironmentVariable;

    /// @brief Creates and returns one scenario-owned directory below the isolated log root.
    std::filesystem::path testDirectory(TestContext &context, std::string_view name)
    {
        std::filesystem::path directory = context.logRoot / std::string(name);
        TestSupport::createDirectories(directory);
        return directory;
    }

    /// @brief Reads one complete log fixture through TestSupport.
    std::string readWholeFile(const std::filesystem::path &path)
    {
        return TestSupport::readTextFile(path);
    }

    /// @brief Counts non-overlapping occurrences in captured log text.
    std::size_t countOccurrences(std::string_view text, std::string_view needle)
    {
        if (needle.empty())
        {
            return 0;
        }

        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = text.find(needle, position)) != std::string_view::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    /// @brief Returns regular files directly contained in one scenario directory.
    std::vector<std::filesystem::path> filesIn(const std::filesystem::path &directory)
    {
        std::vector<std::filesystem::path> files;
        if (!std::filesystem::exists(directory))
        {
            return files;
        }

        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_regular_file())
            {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    /// @brief Concatenates every regular log file in one scenario directory.
    std::string readDirectoryFiles(const std::filesystem::path &directory)
    {
        std::string contents;
        for (const std::filesystem::path &path : filesIn(directory))
        {
            contents += readWholeFile(path);
        }
        return contents;
    }

    /// @brief Owns text referenced by Config string views until Logger::init() copies it.
    struct OwnedLoggerConfig : Logger::Types::Config
    {
        std::string ownedLogDirectory;

        const Logger::Types::Config &ready()
        {
            logDirectory = ownedLogDirectory;
            return *this;
        }
    };

    /// @brief Creates a deterministic bounded Logger configuration for correctness tests.
    OwnedLoggerConfig makeConfig(Logger::Types::Output output, Logger::Types::Level minLevel, const std::filesystem::path &directory)
    {
        OwnedLoggerConfig config;
        config.output = output;
        config.minLevel = minLevel;
        config.maxQueueSize = 1024;
        config.maxMessageLength = 512;
        config.inlineMessageCapacity = 128;
        config.workerBatchSize = 128;
        config.ownedLogDirectory = pathText(directory);
        config.logDirectory = config.ownedLogDirectory;
        config.fallbackToConsoleOnFileFailure = false;
        config.enableConsoleColor = false;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        config.flushFileEveryBatch = false;
        config.flushConsoleEveryWrite = false;
        config.releaseMessageMemoryAfterWrite = true;
        config.releaseStorageOnShutdown = true;
        return config;
    }

    /// @brief Creates a file-only configuration rooted in one isolated scenario directory.
    OwnedLoggerConfig makeFileConfig(TestContext &context, std::string_view name, Logger::Types::Level minLevel = Logger::Types::Level::Trace)
    {
        return makeConfig(Logger::Types::Output::File, minLevel, testDirectory(context, name));
    }

    /// @brief Creates a deterministic console-only configuration without popups or debug mirroring.
    Logger::Types::Config makeConsoleConfig(Logger::Types::Level minLevel = Logger::Types::Level::Trace)
    {
        Logger::Types::Config config;
        config.output = Logger::Types::Output::Console;
        config.minLevel = minLevel;
        config.maxQueueSize = 256;
        config.maxMessageLength = 512;
        config.inlineMessageCapacity = 128;
        config.workerBatchSize = 64;
        config.enableConsoleColor = false;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        config.flushConsoleEveryWrite = false;
        config.releaseMessageMemoryAfterWrite = true;
        config.releaseStorageOnShutdown = true;
        return config;
    }

#if INTERNAL_TERMINAL_TEST_HOOKS
    namespace TerminalHooks = GameWIP::Terminal::TestHooks;

    /// @brief Prevents Terminal hook state from leaking out of an integration scenario.
    struct ScopedTerminalHookReset
    {
        ScopedTerminalHookReset()
        {
            TerminalHooks::reset();
        }

        ~ScopedTerminalHookReset()
        {
            TerminalHooks::reset();
        }
    };

    /// @brief Returns interactive output capabilities used by Logger-to-Terminal integration tests.
    Terminal::Types::OutputCapabilities loggerTerminalCapabilities() noexcept
    {
        return {
            .kind = Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteOutput = true,
            .supportsFlush = true,
            .style = Terminal::Types::StyleCapabilities{.basicColor = true}};
    }

    /// @brief Returns redirected output capabilities that intentionally reject styles.
    Terminal::Types::OutputCapabilities loggerRedirectedCapabilities() noexcept
    {
        return {.kind = Terminal::Types::StreamKind::Redirected, .supportsUtf8Text = true, .supportsByteOutput = true, .supportsFlush = true};
    }

    /// @brief Captures one Terminal stream through the shared runtime hook state.
    void captureTerminalOutput(Terminal::Types::OutputStream stream, const Terminal::Types::OutputCapabilities &capabilities)
    {
        TerminalHooks::setOutputCapabilitiesOverride(stream, capabilities);
        TerminalHooks::setOutputCapture(stream, true);
        TerminalHooks::clearCapturedOutput(stream);
    }
#endif

    /// @brief Records initialization outcome and includes structured platform diagnostics on failure.
    void expectInitSuccess(TestContext &context, std::string_view name, Logger::Types::Result result)
    {
        expectEq(context, name, result, Logger::Types::Result::Success);
        if (result != Logger::Types::Result::Success)
        {
            const Logger::Types::PlatformError platformError = Logger::getLastPlatformError();
            context.fail(
                std::format("{} platform error", name),
                std::format("source={} native={}", static_cast<int>(platformError.source), platformError.nativeCode));
        }
    }

    /// @brief Records evaluation of an argument used to verify lazy logging macros.
    std::string makeLazyArgument(std::atomic<int> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
        return "lazy argument evaluated";
    }

    /// @brief Verifies default, low-memory, and throughput configuration factories.
    void testConfigFactories(TestContext &context)
    {
        ZoneScopedN("Logger config factory tests");

        const Logger::Types::Config defaults = Logger::defaultConfig();
        expectEq(context, "defaultConfig output", defaults.output, Logger::Types::Output::Both);
        expectEq(context, "defaultConfig min level", defaults.minLevel, Logger::Types::Level::Info);
        context.expectTrue("defaultConfig queue positive", defaults.maxQueueSize > 0);
        context.expectTrue("defaultConfig message positive", defaults.maxMessageLength > 0);
        context.expectFalse("defaultConfig reuses message storage", defaults.releaseMessageMemoryAfterWrite);

        const Logger::Types::Config lowMemory = Logger::lowMemoryConfig();
        context.expectTrue("lowMemoryConfig lower queue", lowMemory.maxQueueSize < defaults.maxQueueSize);
        context.expectTrue("lowMemoryConfig lower message length", lowMemory.maxMessageLength < defaults.maxMessageLength);
        context.expectTrue("lowMemoryConfig releases message storage", lowMemory.releaseMessageMemoryAfterWrite);

        const Logger::Types::Config throughput = Logger::throughputConfig();
        context.expectTrue("throughputConfig higher queue", throughput.maxQueueSize > defaults.maxQueueSize);
        context.expectFalse("throughputConfig reuses message storage", throughput.releaseMessageMemoryAfterWrite);
        context.expectFalse("throughputConfig retains storage", throughput.releaseStorageOnShutdown);
    }

    /// @brief Verifies disabled output and invalid initialization inputs leave stable state.
    void testDisabledAndInvalidInit(TestContext &context)
    {
        ZoneScopedN("Logger disabled and invalid init tests");
        ScopedLoggerShutdown shutdown;

        Logger::Types::Config disabled = makeConsoleConfig(Logger::Types::Level::Trace);
        disabled.output = Logger::Types::Output::None;
        const Logger::Types::Result disabledResult = Logger::init(disabled);
        expectEq(context, "init output none succeeds", disabledResult, Logger::Types::Result::Success);
        context.expectFalse("output none is not running", Logger::isRunning());
        context.expectFalse("output none shouldLog false", Logger::shouldLog(Logger::Types::Level::Info));
        Logger::info(testSource, "not accepted");
        const Logger::Types::Stats disabledStats = Logger::getStats();
        expectEq(context, "output none queued zero", disabledStats.queued, std::size_t{0});
        expectEq(context, "output none written zero", disabledStats.written, std::size_t{0});
        Logger::shutdown();

        Logger::Types::Config invalidOutput = makeConsoleConfig();
        invalidOutput.output = static_cast<Logger::Types::Output>(99);
        expectEq(context, "invalid output rejected", Logger::init(invalidOutput), Logger::Types::Result::InvalidOutputMode);
        context.expectFalse("invalid output not running", Logger::isRunning());
        Logger::shutdown();

        Logger::Types::Config invalidLevel = makeConsoleConfig();
        invalidLevel.minLevel = static_cast<Logger::Types::Level>(99);
        expectEq(context, "invalid min level rejected", Logger::init(invalidLevel), Logger::Types::Result::InvalidLevelFilter);
        Logger::shutdown();

        Logger::Types::Config invalidLevelFilter = makeConsoleConfig();
        std::array invalidLevelFilters{Logger::Types::LevelFilter{static_cast<Logger::Types::Level>(99), true}};
        invalidLevelFilter.levelFilters = invalidLevelFilters;
        expectEq(context, "invalid level filter rejected", Logger::init(invalidLevelFilter), Logger::Types::Result::InvalidLevelFilter);
        Logger::shutdown();

        Logger::Types::Config duplicateLevelFilter = makeConsoleConfig();
        std::array duplicateLevelFilters{
            Logger::Types::LevelFilter{Logger::Types::Level::Info, false},
            Logger::Types::LevelFilter{Logger::Types::Level::Info, true}};
        duplicateLevelFilter.levelFilters = duplicateLevelFilters;
        expectEq(context, "duplicate level filter rejected", Logger::init(duplicateLevelFilter), Logger::Types::Result::InvalidLevelFilter);
        Logger::shutdown();

        Logger::Types::Config invalidQueue = makeConsoleConfig();
        invalidQueue.maxQueueSize = 0;
        const Logger::Types::Result invalidQueueResult = Logger::init(invalidQueue);
        expectEq(context, "zero queue sanitized result", invalidQueueResult, Logger::Types::Result::InvalidQueueSize);
        context.expectTrue("zero queue still starts sanitized logger", Logger::isRunning());
        Logger::shutdown();

        Logger::Types::Config invalidMessageLength = makeConsoleConfig();
        invalidMessageLength.maxMessageLength = 0;
        const Logger::Types::Result invalidMessageResult = Logger::init(invalidMessageLength);
        expectEq(context, "zero message length sanitized result", invalidMessageResult, Logger::Types::Result::InvalidMessageLength);
        context.expectTrue("zero message length still starts sanitized logger", Logger::isRunning());
    }

    /// @brief Verifies convenience initialization APIs and runtime default-directory resolution.
    void testConvenienceInitApis(TestContext &context)
    {
        ZoneScopedN("Logger convenience init API tests");
        ScopedLoggerShutdown shutdown;

        expectEq(context, "initConsole succeeds", Logger::initConsole(Logger::Types::Level::Warn), Logger::Types::Result::Success);
        expectEq(context, "initConsole output", Logger::getOutput(), Logger::Types::Output::Console);
        expectEq(context, "initConsole min level", Logger::getMinLevel(), Logger::Types::Level::Warn);
        context.expectFalse("initConsole filters info", Logger::shouldLog(Logger::Types::Level::Info));
        context.expectTrue("initConsole allows error", Logger::shouldLog(Logger::Types::Level::Error));
        Logger::shutdown();

        const std::string initFileDirectory = pathText(testDirectory(context, "init-file-convenience"));
        expectInitSuccess(context, "initFile explicit directory succeeds", Logger::initFile(initFileDirectory, Logger::Types::Level::Info));
        Logger::info(testSource, "initFile visible");
        context.expectTrue("initFile flush succeeds", Logger::flush(2s));
        const std::string initFilePath = Logger::getLogFilePath();
        context.expectFalse("initFile path available", initFilePath.empty());
        Logger::shutdown();
        context.expectTrue("initFile wrote content", readWholeFile(initFilePath).find("initFile visible") != std::string::npos);

        {
            const TestSupport::ScopedCurrentPath currentPath(context.logRoot);
            const Logger::Types::Result defaultResult = Logger::initDefault();
            const bool defaultStarted = defaultResult == Logger::Types::Result::Success || defaultResult == Logger::Types::Result::FileOpenFailed ||
                                        defaultResult == Logger::Types::Result::FileSetupFailed;
            context.expectTrue("initDefault starts or falls back", defaultStarted);
            context.expectTrue("initDefault leaves logger running", Logger::isRunning());
            if (defaultResult == Logger::Types::Result::Success)
            {
                expectEq(context, "initDefault output", Logger::getOutput(), Logger::Types::Output::Both);
                context.expectFalse("initDefault path available", Logger::getLogFilePath().empty());
                context.expectEq(
                    "initDefault resolves logs beneath the working directory",
                    std::filesystem::absolute(Logger::getLogFilePath()).parent_path().lexically_normal(),
                    (context.logRoot / "logs").lexically_normal());
            }
            else
            {
                expectEq(context, "initDefault file failure falls back to console", Logger::getOutput(), Logger::Types::Output::Console);
            }
            Logger::shutdown();
        }
    }

    /// @brief Verifies file sink creation, formatting, flushing, and message content.
    void testFileOutputAndContent(TestContext &context)
    {
        ZoneScopedN("Logger file output tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "file-output");
        config.maxQueueSize = 256;
        config.maxMessageLength = 128;
        config.flushFileEveryBatch = true;
        const Logger::Types::Result result = Logger::init(config.ready());
        expectInitSuccess(context, "file init succeeds", result);
        context.expectTrue("file logger running", Logger::isRunning());
        expectEq(context, "file logger output mode", Logger::getOutput(), Logger::Types::Output::File);
        expectEq(context, "file logger min level", Logger::getMinLevel(), Logger::Types::Level::Trace);

        Logger::trace(testSource, "trace line");
        Logger::debug(testSource, "debug {}", 7);
        Logger::info(testSource, Logger::runtimeFormat("runtime {}"), 11);
        Logger::warn(testSource, "warn line");
        Logger::error(testSource, "error line");
        Logger::fatal(testSource, "fatal line");
        context.expectTrue("file flush succeeds", Logger::flush(2s));

        const std::string logFile = Logger::getLogFilePath();
        context.expectFalse("file path available before shutdown", logFile.empty());
        const Logger::Types::Stats stats = Logger::getStats();
        expectEq(context, "file output queued all levels", stats.queued, std::size_t{6});
        expectEq(context, "file output wrote all levels", stats.written, std::size_t{6});
        expectEq(context, "file output no queue drops", totalQueueDrops(stats), std::size_t{0});
        expectEq(context, "file output no diagnostics", totalDiagnosticFailures(stats), std::size_t{0});
        Logger::shutdown();

        const std::string contents = readWholeFile(logFile);
        context.expectTrue("file contains trace", contents.find("[TRACE][LoggerTest]: trace line") != std::string::npos);
        context.expectTrue("file contains formatted debug", contents.find("[DEBUG][LoggerTest]: debug 7") != std::string::npos);
        context.expectTrue("file contains runtime format", contents.find("[INFO][LoggerTest]: runtime 11") != std::string::npos);
        context.expectTrue("file contains fatal", contents.find("[FATAL][LoggerTest]: fatal line") != std::string::npos);
    }

    /// @brief Verifies Logger's FileSystem path boundary, sharing mode, and UTF-8 path reporting.
    void testFoundationFileSink(TestContext &context)
    {
        ZoneScopedN("Logger foundation file sink tests");
        ScopedLoggerShutdown shutdown;

        const std::filesystem::path unicodeDirectory = context.logRoot / std::filesystem::path(u8"unicode-\u2605");
        OwnedLoggerConfig config = makeConfig(Logger::Types::Output::File, Logger::Types::Level::Trace, unicodeDirectory);
        config.flushFileEveryBatch = true;
        expectInitSuccess(context, "UTF-8 file sink init", Logger::init(config.ready()));

        Logger::info(testSource, "foundation file sink");
        context.expectTrue("UTF-8 file sink flush", Logger::flush(2s));

        const std::string logFileText = Logger::getLogFilePath();
        context.expectContains("UTF-8 log path round trip", logFileText, "\xE2\x98\x85");
        const std::filesystem::path logFile = pathFromText(logFileText);
        context.expectTrue(
            "file sink permits live readers",
            readWholeFile(logFile).find("foundation file sink") != std::string::npos,
            "flushed log file was not readable while Logger retained its writer");

        Logger::shutdown();
        context.expectTrue("UTF-8 log file exists after shutdown", TestSupport::fileExists(logFile));
    }

    /// @brief Verifies Logger routes complete console records through the process-wide Terminal runtime.
    void testTerminalConsoleSink(TestContext &context)
    {
#if INTERNAL_TERMINAL_TEST_HOOKS
        ZoneScopedN("Logger Terminal console sink tests");
        ScopedLoggerShutdown shutdown;
        const ScopedTerminalHookReset terminalHookReset;

        captureTerminalOutput(Terminal::Types::OutputStream::Stdout, loggerTerminalCapabilities());
        captureTerminalOutput(Terminal::Types::OutputStream::Stderr, loggerTerminalCapabilities());

        Logger::Types::Config config = makeConsoleConfig();
        config.enableConsoleColor = true;
        expectInitSuccess(context, "Terminal console sink init", Logger::init(config));
        Logger::trace(testSource, "terminal trace");
        Logger::debug(testSource, "terminal debug");
        Logger::info(testSource, "terminal info");
        Logger::warn(testSource, "terminal warn");
        Logger::error(testSource, "terminal error");
        Logger::fatal(testSource, "terminal fatal");
        context.expectTrue("Terminal console sink flush", Logger::flush(2s));
        Logger::shutdown();

        const std::string stdoutText = TerminalHooks::capturedOutputText(Terminal::Types::OutputStream::Stdout);
        const std::string stderrText = TerminalHooks::capturedOutputText(Terminal::Types::OutputStream::Stderr);
        context.expectContains("Terminal trace color", stdoutText, "\x1b[90m");
        context.expectContains("Terminal debug color", stdoutText, "\x1b[36m");
        context.expectContains("Terminal info output", stdoutText, "[INFO][LoggerTest]: terminal info\r\n");
        context.expectContains("Terminal warning color", stdoutText, "\x1b[33m");
        context.expectContains("Terminal error color", stderrText, "\x1b[31m");
        context.expectContains("Terminal fatal route", stderrText, "[FATAL][LoggerTest]: terminal fatal");
        context.expectEq(
            "Terminal stdout one write per record",
            TerminalHooks::textWriteCallCount(Terminal::Types::OutputStream::Stdout),
            std::size_t{4});
        context.expectEq(
            "Terminal stderr one write per record",
            TerminalHooks::textWriteCallCount(Terminal::Types::OutputStream::Stderr),
            std::size_t{2});
        expectEq(context, "Terminal stdout native line endings", countOccurrences(stdoutText, "\r\n"), std::size_t{4});
        expectEq(context, "Terminal stderr native line endings", countOccurrences(stderrText, "\r\n"), std::size_t{2});

        TerminalHooks::reset();
        captureTerminalOutput(Terminal::Types::OutputStream::Stdout, loggerRedirectedCapabilities());
        config = makeConsoleConfig(Logger::Types::Level::Warn);
        config.enableConsoleColor = true;
        expectInitSuccess(context, "redirected Terminal sink init", Logger::init(config));
        Logger::warn(testSource, "terminal unicode \xE2\x98\x85");
        context.expectTrue("redirected Terminal sink flush", Logger::flush(2s));
        Logger::shutdown();

        const std::string redirected = TerminalHooks::capturedOutputText(Terminal::Types::OutputStream::Stdout);
        context.expectContains("redirected Terminal preserves UTF-8", redirected, "terminal unicode \xE2\x98\x85");
        context.expectTrue("redirected Terminal omits style sequences", redirected.find("\x1b[") == std::string::npos);
        context.expectTrue("redirected Terminal uses native line ending", redirected.ends_with("\r\n"));
        TerminalHooks::reset();
#else
        context.pass("Logger Terminal integration hooks skipped because INTERNAL_TERMINAL_TEST_HOOKS=0");
#endif
    }

    /// @brief Verifies process-wide lifecycle transitions and runtime state queries.
    void testLifecycleAndQueries(TestContext &context)
    {
        ZoneScopedN("Logger lifecycle and query tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "lifecycle", Logger::Types::Level::Info);
        config.maxQueueSize = 32;
        config.hardQueueMultiplier = 2.0;
        config.maxMessageLength = 96;
        config.inlineMessageCapacity = 32;
        config.workerBatchSize = 8;
        expectInitSuccess(context, "lifecycle init", Logger::init(config.ready()));

        const Logger::Types::QueueLimits limits = Logger::getQueueLimits();
        expectEq(context, "queue soft limit", limits.softQueueSize, std::size_t{32});
        expectEq(context, "queue hard limit", limits.hardQueueSize, std::size_t{64});
        expectEq(context, "queue max message length", limits.maxMessageLength, std::size_t{96});
        expectEq(context, "queue inline capacity", limits.inlineMessageCapacity, std::size_t{32});
        expectEq(context, "queue worker batch size", limits.workerBatchSize, std::size_t{8});

        expectEq(context, "already running rejected", Logger::init(config.ready()), Logger::Types::Result::AlreadyRunning);
        context.expectTrue("last result already running", Logger::getLastResult() == Logger::Types::Result::AlreadyRunning);
        Logger::resetStats();
        const Logger::Types::Stats resetStats = Logger::getStats();
        expectEq(context, "reset stats queued zero", resetStats.queued, std::size_t{0});

        const Logger::Types::MemoryStats memory = Logger::getMemoryStats();
        context.expectTrue("memory stats retained bytes", memory.loggerRetainedBytes > 0);
        context.expectTrue("memory stats queue bytes", memory.queueStorageBytes > 0);
        context.expectTrue("memory stats message arena bytes", memory.messageArenaBytes > 0);

        Logger::shutdown();
        context.expectFalse("shutdown clears running", Logger::isRunning());
        expectEq(context, "shutdown output none", Logger::getOutput(), Logger::Types::Output::None);
        expectEq(context, "shutdown clears log path", Logger::getLogFilePath().size(), std::size_t{0});
    }

    /// @brief Verifies severity/source filtering and intentional-skip accounting.
    void testLevelAndSourceFilters(TestContext &context)
    {
        ZoneScopedN("Logger level and source filter tests");
        ScopedLoggerShutdown shutdown;

        std::array sources{
            Logger::defineSource(TestSource::Core, "Core"),
            Logger::defineSource(TestSource::Render, "Render"),
            Logger::defineSource(TestSource::Audio, "Audio")};
        std::array sourceFilters{Logger::Types::SourceFilter{static_cast<Logger::Types::SourceId>(TestSource::Render), false}};
        std::array levelFilters{Logger::Types::LevelFilter{Logger::Types::Level::Debug, false}};

        OwnedLoggerConfig config = makeFileConfig(context, "filters", Logger::Types::Level::Trace);
        config.sources = sources;
        config.sourceFilters = sourceFilters;
        config.levelFilters = levelFilters;
        expectInitSuccess(context, "filter init", Logger::init(config.ready()));

        context.expectTrue("core source allowed", Logger::shouldLog(Logger::Types::Level::Info, TestSource::Core));
        context.expectFalse("render source initially filtered", Logger::shouldLog(Logger::Types::Level::Info, TestSource::Render));
        context.expectFalse("debug level initially filtered", Logger::shouldLog(Logger::Types::Level::Debug));

        Logger::debug(TestSource::Core, "filtered debug");
        Logger::info(TestSource::Render, "filtered render");
        Logger::info(TestSource::Core, "core visible");
        Logger::info(TestSource::Unknown, "unknown source visible");
        context.expectTrue("filter flush", Logger::flush(2s));

        Logger::Types::Stats stats = Logger::getStats();
        expectEq(context, "filter queued accepted entries", stats.queued, std::size_t{2});
        expectEq(context, "filtered entries are not queue drops", totalQueueDrops(stats), std::size_t{0});
        expectEq(context, "filtered entries are not diagnostics", totalDiagnosticFailures(stats), std::size_t{0});
        expectEq(context, "unknown source counted", stats.unknownSourceUses, std::size_t{1});

        expectEq(context, "set source filter succeeds", Logger::setSourceFilter(TestSource::Render, true), Logger::Types::Result::Success);
        expectEq(context, "clear source filter succeeds", Logger::clearSourceFilter(TestSource::Render), Logger::Types::Result::Success);
        expectEq(
            context,
            "unknown source filter rejected",
            Logger::setSourceFilter(TestSource::Unknown, false),
            Logger::Types::Result::InvalidSourceFilter);
        Logger::clearSourceFilters();
        context.expectTrue("render source allowed after clear", Logger::shouldLog(Logger::Types::Level::Info, TestSource::Render));

        expectEq(context, "set level filter succeeds", Logger::setLevelFilter(Logger::Types::Level::Info, false), Logger::Types::Result::Success);
        context.expectFalse("info level filtered", Logger::shouldLog(Logger::Types::Level::Info));
        Logger::info(TestSource::Core, "filtered info");
        expectEq(context, "clear level filter succeeds", Logger::clearLevelFilter(Logger::Types::Level::Info), Logger::Types::Result::Success);
        Logger::clearLevelFilters();
        context.expectTrue("debug level allowed after clear", Logger::shouldLog(Logger::Types::Level::Debug));
        expectEq(
            context,
            "invalid runtime level filter rejected",
            Logger::setLevelFilter(static_cast<Logger::Types::Level>(99), true),
            Logger::Types::Result::InvalidLevelFilter);
    }

    /// @brief Verifies source definition/filter validation and unknown-source behavior.
    void testSourceValidation(TestContext &context)
    {
        ZoneScopedN("Logger source validation tests");
        ScopedLoggerShutdown shutdown;

        std::array emptyNameSources{Logger::Types::SourceDefinition{1, {}}};
        Logger::Types::Config emptyNameConfig = makeConsoleConfig();
        emptyNameConfig.sources = emptyNameSources;
        expectEq(context, "empty source name rejected", Logger::init(emptyNameConfig), Logger::Types::Result::InvalidSourceDefinition);
        Logger::shutdown();

        std::array duplicateSources{Logger::Types::SourceDefinition{1, "Core"}, Logger::Types::SourceDefinition{1, "CoreDuplicate"}};
        Logger::Types::Config duplicateConfig = makeConsoleConfig();
        duplicateConfig.sources = duplicateSources;
        expectEq(context, "duplicate source rejected", Logger::init(duplicateConfig), Logger::Types::Result::InvalidSourceDefinition);
        Logger::shutdown();

        std::array validSources{Logger::Types::SourceDefinition{1, "Core"}};
        std::array invalidFilters{Logger::Types::SourceFilter{99, false}};
        Logger::Types::Config invalidFilterConfig = makeConsoleConfig();
        invalidFilterConfig.sources = validSources;
        invalidFilterConfig.sourceFilters = invalidFilters;
        expectEq(context, "unknown source filter rejected at init", Logger::init(invalidFilterConfig), Logger::Types::Result::InvalidSourceFilter);
        Logger::shutdown();

        std::array duplicateFilters{Logger::Types::SourceFilter{1, false}, Logger::Types::SourceFilter{1, true}};
        Logger::Types::Config duplicateFilterConfig = makeConsoleConfig();
        duplicateFilterConfig.sources = validSources;
        duplicateFilterConfig.sourceFilters = duplicateFilters;
        expectEq(
            context,
            "duplicate source filter rejected at init",
            Logger::init(duplicateFilterConfig),
            Logger::Types::Result::InvalidSourceFilter);
    }

    /// @brief Verifies strict/runtime formatting, invalid formats, and bounded truncation.
    void testFormattingAndTruncation(TestContext &context)
    {
        ZoneScopedN("Logger formatting and truncation tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig strictConfig = makeFileConfig(context, "format-strict", Logger::Types::Level::Trace);
        strictConfig.maxMessageLength = 48;
        strictConfig.inlineMessageCapacity = 16;
        strictConfig.formatPolicy = Logger::Types::FormatPolicy::StrictBounded;
        expectInitSuccess(context, "strict format init", Logger::init(strictConfig.ready()));

        Logger::info(testSource, "value {} {}", 12, "ok");
        Logger::info(testSource, Logger::runtimeFormat("runtime {} {}"), 13, "ok");
        Logger::info(testSource, Logger::runtimeFormat("{"), 1);
        Logger::info(testSource, "long {}", std::string(256, 'x'));
        Logger::info(testSource, "reentrant {}", LoggerReentrantFormat{});
        context.expectTrue("strict format flush", Logger::flush(2s));
        Logger::Types::Stats strictStats = Logger::getStats();
        expectEq(context, "strict format queued", strictStats.queued, std::size_t{5});
        expectEq(context, "strict runtime format failure counted", strictStats.formatFailures, std::size_t{1});
        expectEq(context, "strict truncation counted", strictStats.truncated, std::size_t{1});
        const std::string strictPath = Logger::getLogFilePath();
        Logger::shutdown();

        const std::string strictContents = readWholeFile(strictPath);
        context.expectTrue("strict compile format content", strictContents.find("value 12 ok") != std::string::npos);
        context.expectTrue("strict runtime format content", strictContents.find("runtime 13 ok") != std::string::npos);
        context.expectTrue("strict truncation suffix content", strictContents.find("[truncated]") != std::string::npos);
        context.expectTrue("reentrant formatter nested content", strictContents.find("nested 7") != std::string::npos);
        context.expectTrue("reentrant formatter outer content", strictContents.find("reentrant outer") != std::string::npos);

        OwnedLoggerConfig fastConfig = makeFileConfig(context, "format-fast", Logger::Types::Level::Trace);
        fastConfig.maxMessageLength = 48;
        fastConfig.formatPolicy = Logger::Types::FormatPolicy::FastNormal;
        expectInitSuccess(context, "fast format init", Logger::init(fastConfig.ready()));
        Logger::info(testSource, "fast {}", std::string(256, 'y'));
        context.expectTrue("fast format flush", Logger::flush(2s));
        const Logger::Types::Stats fastStats = Logger::getStats();
        expectEq(context, "fast truncation counted", fastStats.truncated, std::size_t{1});
    }

    /// @brief Verifies synchronous report behavior, filter bypass, and debug-output mirroring.
    void testReportsAndDebugOutput(TestContext &context)
    {
        ZoneScopedN("Logger report and debug output tests");
        ScopedLoggerShutdown shutdown;

        std::array sources{Logger::defineSource(TestSource::Core, "Core")};
        OwnedLoggerConfig config = makeFileConfig(context, "reports", Logger::Types::Level::Fatal);
        config.sources = sources;
        config.enableDebugOutput = true;
        config.enableFatalPopup = false;
        expectInitSuccess(context, "report init", Logger::init(config.ready()));

        context.expectFalse("normal info below min", Logger::shouldLog(Logger::Types::Level::Info));
        Logger::reportError(testSource, "plain report");
        context.expectTrue("timeout report plain", Logger::reportError(testSource, Logger::flushTimeout(2s), "timeout report"));
        Logger::reportError(testSource, "formatted report {}", 21);
        Logger::reportFatal(testSource, "fatal wrapper report");
        Logger::report(Logger::Types::Level::Warn, testSource, "generic report {}", 23);
        context.expectTrue(
            "source runtime report",
            Logger::report(Logger::Types::Level::Fatal, TestSource::Core, Logger::flushTimeout(2s), Logger::runtimeFormat("runtime fatal {}"), 22));
        Logger::writeDebugOutput(Logger::Types::Level::Error, testSource, "debug output direct");
        context.expectTrue("report flush", Logger::flush(2s));

        const Logger::Types::Stats stats = Logger::getStats();
        expectEq(context, "reports bypass min without queueing", stats.queued, std::size_t{0});
        expectEq(context, "reports written synchronously", stats.written, std::size_t{6});
        const std::string logFile = Logger::getLogFilePath();
        Logger::shutdown();

        const std::string contents = readWholeFile(logFile);
        context.expectTrue("report file plain", contents.find("plain report") != std::string::npos);
        context.expectTrue("report file formatted", contents.find("formatted report 21") != std::string::npos);
        context.expectTrue("report fatal wrapper file", contents.find("[FATAL][LoggerTest]: fatal wrapper report") != std::string::npos);
        context.expectTrue("report file generic", contents.find("[WARN][LoggerTest]: generic report 23") != std::string::npos);
        context.expectTrue("report file runtime source", contents.find("[FATAL][Core]: runtime fatal 22") != std::string::npos);
    }

    /// @brief Verifies report sink failures and unknown registered-source fallback paths.
    void testReportFailureAndUnknownSourcePaths(TestContext &context)
    {
        ZoneScopedN("Logger report failure and unknown-source tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "report-edge-paths", Logger::Types::Level::Fatal);
        std::array sources{Logger::defineSource(TestSource::Core, "Core")};
        std::array sourceFilters{Logger::Types::SourceFilter{static_cast<Logger::Types::SourceId>(TestSource::Core), false}};
        std::array levelFilters{Logger::Types::LevelFilter{Logger::Types::Level::Error, false}};
        config.sources = sources;
        config.sourceFilters = sourceFilters;
        config.levelFilters = levelFilters;
        expectInitSuccess(context, "report edge init", Logger::init(config.ready()));
        Logger::resetStats();

        Logger::report(Logger::Types::Level::Error, TestSource::Core, "report bypasses disabled core source and error level");
        Logger::report(Logger::Types::Level::Error, TestSource::Unknown, "report unknown source path");
        Logger::report(Logger::Types::Level::Error, testSource, Logger::runtimeFormat("{"), 1);

        const Logger::Types::Stats stats = Logger::getStats();
        expectEq(context, "report edge entries not queued", stats.queued, std::size_t{0});
        expectEq(context, "report edge writes successful reports", stats.written, std::size_t{2});
        expectEq(context, "report runtime format failure counted", stats.formatFailures, std::size_t{1});
        expectEq(context, "report unknown source counted", stats.unknownSourceUses, std::size_t{1});
        expectEq(context, "report edge no queue drops", totalQueueDrops(stats), std::size_t{0});

        const std::string logFile = Logger::getLogFilePath();
        Logger::shutdown();
        const std::string contents = readWholeFile(logFile);
        context.expectTrue(
            "report bypassed filters reached file",
            contents.find("report bypasses disabled core source and error level") != std::string::npos);
        context.expectTrue("report unknown source reached file", contents.find("report unknown source path") != std::string::npos);
        context.expectTrue("bad report format not emitted", contents.find("{}") == std::string::npos);
    }

    /// @brief Verifies each one-shot Logger failure hook and its observable counter/error state.
    void testLoggerTestHooks(TestContext &context)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        ZoneScopedN("Logger test hook tests");
        GameWIP::Logger::TestHooks::reset();

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-file-open", Logger::Types::Level::Trace);
            GameWIP::Logger::TestHooks::forceNextFileOpenFailure();
            const Logger::Types::Result result = Logger::init(config.ready());
            expectEq(context, "hook forced file open retries successfully", result, Logger::Types::Result::Success);
            context.expectTrue("hook file open retry leaves logger running", Logger::isRunning());
            Logger::info(testSource, "file open retry still writes");
            context.expectTrue("hook file open retry flush", Logger::flush(2s));
            const std::string logFile = Logger::getLogFilePath();
            context.expectTrue("hook file open retry used next candidate", logFile.ends_with("_1.log"));
            Logger::shutdown();
            context.expectTrue(
                "hook file open retry wrote content",
                readWholeFile(logFile).find("file open retry still writes") != std::string::npos);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-allocation", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook allocation init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::Logger::TestHooks::forceNextQueueAllocationFailure();
            Logger::info(testSource, "forced allocation failure");
            context.expectTrue("hook allocation failure flush", Logger::flush(2s));
            const Logger::Types::Stats stats = Logger::getStats();
            expectEq(context, "hook allocation failure not queued", stats.queued, std::size_t{0});
            context.expectTrue("hook allocation failure counted", stats.allocationFailures > 0);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-file-flush", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook file flush init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::Logger::TestHooks::forceNextFileFlushFailure();
            context.expectFalse("hook forced file flush failure", Logger::flush(2s));
            const Logger::Types::Stats stats = Logger::getStats();
            context.expectTrue("hook file flush failure counted", stats.fileWriteFailures > 0);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-file-write", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook file write init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::Logger::TestHooks::forceNextFileWriteFailure();
            Logger::report(Logger::Types::Level::Error, testSource, "forced file write failure report");
            const Logger::Types::Stats stats = Logger::getStats();
            context.expectTrue("hook file write failure counted", stats.fileWriteFailures > 0);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-fatal-popup", Logger::Types::Level::Trace);
            config.enableFatalPopup = true;
            expectInitSuccess(context, "hook fatal popup init", Logger::init(config.ready()));
            GameWIP::Logger::TestHooks::forceNextFatalPopupFailure();
            Logger::report(Logger::Types::Level::Fatal, testSource, Logger::Types::ReportPopup::Fatal, "forced fatal popup failure");
            const Logger::Types::PlatformError error = Logger::getLastPlatformError();
            context.expectTrue("hook fatal popup failure source", error.source == Logger::Types::PlatformErrorSource::FatalPopup);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-flush-timeout", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook timed flush init", Logger::init(config.ready()));
            GameWIP::Logger::TestHooks::forceNextTimedFlushTimeout();
            context.expectFalse("hook timed flush timeout", Logger::flush(2s));
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-report-timeout", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook report timeout init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::Logger::TestHooks::forceNextTimedFlushTimeout();
            const bool flushed = Logger::reportError(testSource, Logger::flushTimeout(2s), "report timeout still writes first");
            context.expectFalse("hook report timeout returns bounded drain failure", flushed);
            const Logger::Types::Stats stats = Logger::getStats();
            expectEq(context, "hook report timeout not queued", stats.queued, std::size_t{0});
            expectEq(context, "hook report timeout written synchronously", stats.written, std::size_t{1});
            const std::string logFile = Logger::getLogFilePath();
            Logger::shutdown();
            context.expectTrue(
                "hook report timeout line reached file",
                readWholeFile(logFile).find("report timeout still writes first") != std::string::npos);
        }

        GameWIP::Logger::TestHooks::reset();
#else
        context.pass("logger test hooks skipped because INTERNAL_LOGGER_TEST_HOOKS=0");
#endif
    }

    /// @brief Verifies synchronous reports remain writable while the async queue is saturated.
    void testReportBypassesFullQueue(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("report full queue stress skipped by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger report bypasses full queue test");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "report-full-queue", Logger::Types::Level::Trace);
        config.maxQueueSize = 1;
        config.hardQueueMultiplier = 1.0;
        config.workerBatchSize = 1;
        config.flushFileEveryBatch = false;
        config.releaseMessageMemoryAfterWrite = false;
        expectInitSuccess(context, "report full queue init", Logger::init(config.ready()));
        Logger::resetStats();

        const int stressThreads = static_cast<int>(std::max<std::size_t>(2, options.stressThreadCount));
        const int stressIterations = static_cast<int>(std::max<std::size_t>(1000, options.stressIterationsPerThread));
        std::atomic<bool> start{false};
        std::atomic<bool> stop{false};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(stressThreads));

        for (int workerIndex = 0; workerIndex < stressThreads; ++workerIndex)
        {
            workers.emplace_back(
                [&start, &stop, stressIterations]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    for (int i = 0; i < stressIterations && !stop.load(std::memory_order_relaxed); ++i)
                    {
                        Logger::fatal(testSource, "full queue pressure {}", i);
                        if (Logger::getStats().queueDropsHard > 0)
                        {
                            stop.store(true, std::memory_order_relaxed);
                        }
                    }
                });
        }

        start.store(true, std::memory_order_release);
        for (std::thread &worker : workers)
        {
            worker.join();
        }

        const Logger::Types::Stats beforeReport = Logger::getStats();
        context.expectTrue("full queue produced a hard drop", beforeReport.queueDropsHard > 0);
        Logger::report(Logger::Types::Level::Error, testSource, "synchronous report survived full queue");
        const Logger::Types::Stats afterReport = Logger::getStats();
        expectEq(context, "report did not increment queued while full", afterReport.queued, beforeReport.queued);
        expectEq(context, "report did not increment hard drops", afterReport.queueDropsHard, beforeReport.queueDropsHard);
        context.expectTrue("report incremented written synchronously", afterReport.written > beforeReport.written);

        const std::string logFile = Logger::getLogFilePath();
        context.expectTrue("report full queue flush", Logger::flush(5s));
        Logger::shutdown();
        const std::string contents = readWholeFile(logFile);
        context.expectTrue("report survived full queue reached file", contents.find("synchronous report survived full queue") != std::string::npos);
    }

    /// @brief Verifies report ordering and bounded drain behavior during active production.
    void testReportWhileProducersActive(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("report active producer stress skipped by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger report while producers active stress");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "report-while-producers", Logger::Types::Level::Trace);
        config.maxQueueSize = 4096;
        config.workerBatchSize = 64;
        config.flushFileEveryBatch = false;
        expectInitSuccess(context, "report active producers init", Logger::init(config.ready()));

        const int stressThreads = static_cast<int>(std::max<std::size_t>(2, options.stressThreadCount));
        std::atomic<bool> start{false};
        std::atomic<bool> stop{false};
        std::atomic<std::size_t> attempts{0};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(stressThreads));

        for (int workerIndex = 0; workerIndex < stressThreads; ++workerIndex)
        {
            workers.emplace_back(
                [&start, &stop, &attempts]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    while (!stop.load(std::memory_order_acquire))
                    {
                        Logger::info(testSource, "active report producer {}", attempts.fetch_add(1, std::memory_order_relaxed));
                    }
                });
        }

        start.store(true, std::memory_order_release);

        const auto waitStart = Clock::now();
        while (attempts.load(std::memory_order_acquire) < 1024 && Clock::now() - waitStart < 500ms)
        {
            std::this_thread::yield();
        }

        const Logger::Types::Stats beforeReport = Logger::getStats();
        Logger::report(Logger::Types::Level::Error, testSource, "synchronous report while producers active");
        const Logger::Types::Stats afterReport = Logger::getStats();
        context.expectTrue("report active producers attempted logs", attempts.load(std::memory_order_relaxed) > 0);
        context.expectTrue("report active producers wrote synchronously", afterReport.written > beforeReport.written);

        stop.store(true, std::memory_order_release);
        for (std::thread &worker : workers)
        {
            worker.join();
        }

        const std::string logFile = Logger::getLogFilePath();
        context.expectTrue("report active producers final flush", Logger::flush(5s));
        Logger::shutdown();
        const std::string contents = readWholeFile(logFile);
        context.expectTrue("report active producers reached file", contents.find("synchronous report while producers active") != std::string::npos);
    }

    /// @brief Verifies file-setup failure fallback and no-fallback initialization outcomes.
    void testFileFallback(TestContext &context)
    {
        ZoneScopedN("Logger file fallback tests");
        ScopedLoggerShutdown shutdown;

        const std::filesystem::path blockingFile = context.logRoot / "not-a-directory";
        {
            std::ofstream file(blockingFile);
            file << "blocks directory creation";
        }

        OwnedLoggerConfig noFallback = makeConfig(Logger::Types::Output::File, Logger::Types::Level::Info, blockingFile);
        noFallback.fallbackToConsoleOnFileFailure = false;
        const Logger::Types::Result noFallbackResult = Logger::init(noFallback.ready());
        context.expectTrue(
            "file setup failure without fallback reported",
            noFallbackResult == Logger::Types::Result::FileSetupFailed || noFallbackResult == Logger::Types::Result::InvalidLogDirectory);
        expectEq(context, "file setup failure output none", Logger::getOutput(), Logger::Types::Output::None);
        Logger::shutdown();

        OwnedLoggerConfig withFallback = makeConfig(Logger::Types::Output::File, Logger::Types::Level::Fatal, blockingFile);
        withFallback.fallbackToConsoleOnFileFailure = true;
        const Logger::Types::Result fallbackResult = Logger::init(withFallback.ready());
        context.expectTrue(
            "file setup failure with fallback reported",
            fallbackResult == Logger::Types::Result::FileSetupFailed || fallbackResult == Logger::Types::Result::InvalidLogDirectory);
        expectEq(context, "file setup fallback to console", Logger::getOutput(), Logger::Types::Output::Console);
    }

    /// @brief Verifies lazy macro filtering, source routing, and argument evaluation.
    void testMacroBehavior(TestContext &context)
    {
        ZoneScopedN("Logger macro behavior tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "macros", Logger::Types::Level::Fatal);
        expectInitSuccess(context, "macro init", Logger::init(config.ready()));

        std::atomic<int> sideEffects{0};
        LOGGER_INFO(testSource, "filtered {}", makeLazyArgument(sideEffects));
        LOGGER_WARN(testSource, "filtered {}", makeLazyArgument(sideEffects));
        LOGGER_ERROR(testSource, "filtered {}", makeLazyArgument(sideEffects));
        expectEq(context, "macro filters avoid arg evaluation", sideEffects.load(std::memory_order_relaxed), 0);

        LOGGER_FATAL(testSource, "accepted {}", 1);
        context.expectTrue("macro fatal flush", Logger::flush(2s));
        const Logger::Types::Stats stats = Logger::getStats();
        expectEq(context, "macro fatal queued", stats.queued, std::size_t{1});

        std::atomic<int> debugSideEffects{0};
        LOGGER_DEBUG(testSource, "debug maybe compiled {}", makeLazyArgument(debugSideEffects));
#if !defined(NDEBUG) || defined(LOGGER_ENABLE_DEBUG_LOGS)
        expectEq(context, "debug macro active but runtime filtered", debugSideEffects.load(std::memory_order_relaxed), 0);
#else
        expectEq(context, "debug macro compiled out", debugSideEffects.load(std::memory_order_relaxed), 0);
#endif
    }

    /// @brief Verifies queue limits, multi-producer safety, drop counters, and eventual draining.
    void testQueuePressureAndConcurrency(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("stress tests disabled by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger pressure and concurrency tests");
        ScopedLoggerShutdown shutdown;
        const int stressThreads = static_cast<int>(std::max<std::size_t>(1, options.stressThreadCount));
        const int stressIterations = static_cast<int>(std::max<std::size_t>(1, options.stressIterationsPerThread));

        {
            OwnedLoggerConfig config = makeFileConfig(context, "soft-pressure", Logger::Types::Level::Trace);
            config.maxQueueSize = 4;
            config.hardQueueMultiplier = 8.0;
            config.workerBatchSize = 1;
            expectInitSuccess(context, "soft pressure init", Logger::init(config.ready()));
            std::vector<std::thread> workers;
            workers.reserve(static_cast<std::size_t>(stressThreads));
            for (int worker = 0; worker < stressThreads; ++worker)
            {
                workers.emplace_back(
                    [stressIterations]
                    {
                        for (int i = 0; i < stressIterations; ++i)
                        {
                            Logger::debug(testSource, "soft pressure");
                        }
                    });
            }
            for (std::thread &worker : workers)
            {
                worker.join();
            }
            context.expectTrue("soft pressure final flush", Logger::flush(5s));
            const Logger::Types::Stats stats = Logger::getStats();
            context.expectTrue("soft pressure queued something", stats.queued > 0);
            context.expectTrue("soft pressure dropped low priority", stats.queueDropsSoft > 0);
            recordMemorySnapshot(context, "soft-pressure");
            Logger::shutdown();
        }

        {
            OwnedLoggerConfig config = makeFileConfig(context, "hard-pressure", Logger::Types::Level::Trace);
            config.maxQueueSize = 2;
            config.hardQueueMultiplier = 1.0;
            config.workerBatchSize = 1;
            expectInitSuccess(context, "hard pressure init", Logger::init(config.ready()));
            std::vector<std::thread> workers;
            workers.reserve(static_cast<std::size_t>(stressThreads));
            for (int worker = 0; worker < stressThreads; ++worker)
            {
                workers.emplace_back(
                    [stressIterations]
                    {
                        for (int i = 0; i < stressIterations; ++i)
                        {
                            Logger::fatal(testSource, "hard pressure");
                        }
                    });
            }
            for (std::thread &worker : workers)
            {
                worker.join();
            }
            context.expectTrue("hard pressure final flush", Logger::flush(5s));
            const Logger::Types::Stats stats = Logger::getStats();
            context.expectTrue("hard pressure queued something", stats.queued > 0);
            context.expectTrue("hard pressure dropped at hard limit", stats.queueDropsHard > 0);
            recordMemorySnapshot(context, "hard-pressure");
            Logger::shutdown();
        }

        {
            OwnedLoggerConfig config = makeFileConfig(context, "shutdown-race", Logger::Types::Level::Info);
            config.maxQueueSize = 1024;
            expectInitSuccess(context, "shutdown race init", Logger::init(config.ready()));
            recordMemorySnapshot(context, "shutdown-race");
            std::atomic<bool> start{false};
            std::vector<std::thread> workers;
            workers.reserve(static_cast<std::size_t>(stressThreads));
            for (int workerIndex = 0; workerIndex < stressThreads; ++workerIndex)
            {
                workers.emplace_back(
                    [&start, stressIterations]
                    {
                        while (!start.load(std::memory_order_acquire))
                        {
                            std::this_thread::yield();
                        }
                        for (int i = 0; i < stressIterations; ++i)
                        {
                            Logger::info(testSource, "shutdown race");
                        }
                    });
            }
            start.store(true, std::memory_order_release);
            std::this_thread::sleep_for(1ms);
            Logger::shutdown();
            for (std::thread &worker : workers)
            {
                worker.join();
            }
            context.expectFalse("shutdown race leaves logger stopped", Logger::isRunning());
        }
    }

    /// @brief Verifies filtered messages are intentional skips rather than queue drops.
    void testFilteredLogsDoNotCountAsDrops(TestContext &context)
    {
        ZoneScopedN("Logger filtered log stats tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "filtered-stats", Logger::Types::Level::Trace);
        expectInitSuccess(context, "filtered stats init", Logger::init(config.ready()));
        Logger::resetStats();

        const std::size_t lifetimeBeforeFilter = Logger::getLifetimeDroppedLogCount();
        expectEq(context, "disable info level", Logger::setLevelFilter(Logger::Types::Level::Info, false), Logger::Types::Result::Success);
        Logger::info(testSource, "filtered info");

        const Logger::Types::Stats stats = Logger::getStats();
        expectEq(context, "filtered log not queued", stats.queued, std::size_t{0});
        expectEq(context, "filtered log not written", stats.written, std::size_t{0});
        expectEq(context, "filtered log no queue drops", totalQueueDrops(stats), std::size_t{0});
        expectEq(context, "filtered log no diagnostics", totalDiagnosticFailures(stats), std::size_t{0});
        expectEq(context, "filtered log keeps lifetime drops", Logger::getLifetimeDroppedLogCount(), lifetimeBeforeFilter);
    }

    /// @brief Verifies stats reset clears visible counters but preserves lifetime drop totals.
    void testStatsResetKeepsLifetimeQueueDrops(TestContext &context)
    {
        ZoneScopedN("Logger lifetime queue drop reset tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "stats-queue-drops", Logger::Types::Level::Trace);
        config.maxQueueSize = 1;
        config.hardQueueMultiplier = 1.0;
        config.workerBatchSize = 1;
        config.flushFileEveryBatch = true;
        config.releaseMessageMemoryAfterWrite = false;
        expectInitSuccess(context, "stats queue drops init", Logger::init(config.ready()));
        Logger::resetStats();

        const std::size_t lifetimeBeforePressure = Logger::getLifetimeDroppedLogCount();
        const unsigned detectedThreads = std::thread::hardware_concurrency();
        const unsigned pressureThreads = std::clamp(detectedThreads == 0 ? 4u : detectedThreads, 2u, 8u);
        constexpr int pressureIterations = 20'000;

        std::atomic<bool> start{false};
        std::atomic<bool> stop{false};
        std::vector<std::thread> workers;
        workers.reserve(pressureThreads);

        for (unsigned workerIndex = 0; workerIndex < pressureThreads; ++workerIndex)
        {
            workers.emplace_back(
                [&start, &stop, lifetimeBeforePressure]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    for (int i = 0; i < pressureIterations && !stop.load(std::memory_order_relaxed); ++i)
                    {
                        Logger::fatal(testSource, "queue pressure {}", i);
                        if (Logger::getLifetimeDroppedLogCount() > lifetimeBeforePressure)
                        {
                            stop.store(true, std::memory_order_relaxed);
                        }
                    }
                });
        }

        start.store(true, std::memory_order_release);
        for (std::thread &worker : workers)
        {
            worker.join();
        }

        context.expectTrue("stats queue drop flush", Logger::flush(5s));

        const Logger::Types::Stats beforeReset = Logger::getStats();
        const std::size_t lifetimeBeforeReset = Logger::getLifetimeDroppedLogCount();
        context.expectTrue("queue drops before reset", totalQueueDrops(beforeReset) > 0);
        context.expectTrue("lifetime queue drops increased", lifetimeBeforeReset > lifetimeBeforePressure);

        Logger::resetStats();
        const Logger::Types::Stats afterReset = Logger::getStats();
        expectEq(context, "reset clears queued count", afterReset.queued, std::size_t{0});
        expectEq(context, "reset clears written count", afterReset.written, std::size_t{0});
        expectEq(context, "reset clears queue drops", totalQueueDrops(afterReset), std::size_t{0});
        expectEq(context, "reset clears diagnostics", totalDiagnosticFailures(afterReset), std::size_t{0});
        expectEq(context, "reset keeps lifetime queue drops", Logger::getLifetimeDroppedLogCount(), lifetimeBeforeReset);
    }

    /// @brief Waits for observable producer progress with a hard deadline to avoid unbounded stress tests.
    void waitForProducerAttempts(const std::atomic<std::size_t> &attempts, std::size_t minimumAttempts, std::chrono::milliseconds timeout)
    {
        const auto waitStart = Clock::now();
        while (attempts.load(std::memory_order_acquire) < minimumAttempts && Clock::now() - waitStart < timeout)
        {
            std::this_thread::yield();
        }
    }

    /// @brief Verifies timed flush remains bounded and preserves accounting while producers continue.
    void testFlushWhileProducersActive(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("flush active producer stress skipped by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger flush while producers active stress");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "flush-while-producers", Logger::Types::Level::Trace);
        config.maxQueueSize = 2048;
        config.workerBatchSize = 64;
        expectInitSuccess(context, "flush active producers init", Logger::init(config.ready()));

        const int stressThreads = static_cast<int>(std::max<std::size_t>(2, options.stressThreadCount));
        const int flushCount = 64;
        std::atomic<bool> start{false};
        std::atomic<bool> stop{false};
        std::atomic<std::size_t> attempts{0};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(stressThreads));

        for (int workerIndex = 0; workerIndex < stressThreads; ++workerIndex)
        {
            workers.emplace_back(
                [&start, &stop, &attempts]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    while (!stop.load(std::memory_order_acquire))
                    {
                        Logger::info(testSource, "flush active producer {}", attempts.fetch_add(1, std::memory_order_relaxed));
                    }
                });
        }

        start.store(true, std::memory_order_release);
        waitForProducerAttempts(attempts, 1024, 500ms);

        std::size_t flushSuccesses = 0;
        std::size_t flushTimeouts = 0;
        for (int i = 0; i < flushCount; ++i)
        {
            if (Logger::flush(std::chrono::milliseconds{50}))
            {
                ++flushSuccesses;
            }
            else
            {
                ++flushTimeouts;
            }
        }

        context.expectEq("flush active producers attempts completed", flushSuccesses + flushTimeouts, static_cast<std::size_t>(flushCount));
        context.expectTrue("flush active producers attempted bounded flushes", flushCount > 0);

        stop.store(true, std::memory_order_release);

        for (std::thread &worker : workers)
        {
            worker.join();
        }

        context.expectTrue("flush active producers final flush", Logger::flush(5s));
        const Logger::Types::Stats stats = Logger::getStats();
        context.expectTrue("flush active producers attempted logs", attempts.load(std::memory_order_relaxed) > 0);
        context.expectTrue("flush active producers wrote logs", stats.written > 0);
        context.emit(
            std::format(
                "[STRESS] flushWhileProducersActive threads={} flushAttempts={} flushSuccesses={} flushTimeouts={} producerAttempts={} written={} "
                "queueDropped={} peakQueue={}\n",
                stressThreads,
                flushCount,
                flushSuccesses,
                flushTimeouts,
                attempts.load(std::memory_order_relaxed),
                stats.written,
                stats.queueDropsSoft + stats.queueDropsHard,
                stats.peakQueueDepth));
        recordMemorySnapshot(context, "flush-while-producers");
    }

    /// @brief Verifies shutdown closes producer races and joins the worker without lost accepted entries.
    void testShutdownWhileProducersActive(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("shutdown active producer stress skipped by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger shutdown while producers active stress");

        OwnedLoggerConfig config = makeFileConfig(context, "shutdown-while-producers", Logger::Types::Level::Trace);
        config.maxQueueSize = 1024;
        config.workerBatchSize = 64;
        expectInitSuccess(context, "shutdown active producers init", Logger::init(config.ready()));

        const int stressThreads = static_cast<int>(std::max<std::size_t>(2, options.stressThreadCount));
        std::atomic<bool> start{false};
        std::atomic<bool> stop{false};
        std::atomic<std::size_t> attempts{0};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(stressThreads));

        for (int workerIndex = 0; workerIndex < stressThreads; ++workerIndex)
        {
            workers.emplace_back(
                [&start, &stop, &attempts]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    while (!stop.load(std::memory_order_acquire))
                    {
                        Logger::info(testSource, "shutdown active producer {}", attempts.fetch_add(1, std::memory_order_relaxed));
                    }
                });
        }

        start.store(true, std::memory_order_release);
        waitForProducerAttempts(attempts, 1024, 500ms);
        Logger::shutdown();
        std::this_thread::sleep_for(1ms);
        stop.store(true, std::memory_order_release);

        for (std::thread &worker : workers)
        {
            worker.join();
        }

        context.expectFalse("shutdown active producers leaves logger stopped", Logger::isRunning());
        context.expectTrue("shutdown active producers attempted logs", attempts.load(std::memory_order_relaxed) > 0);
    }

    /// @brief Verifies repeated process-wide initialization and shutdown release runtime storage.
    void testRepeatedInitShutdownStress(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("repeated init shutdown stress skipped by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger repeated init shutdown stress");
        constexpr int iterations = 100;

        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            OwnedLoggerConfig config = makeFileConfig(context, std::format("init-shutdown-{}", iteration), Logger::Types::Level::Trace);
            config.maxQueueSize = 64;
            expectInitSuccess(context, "repeated init", Logger::init(config.ready()));
            Logger::info(testSource, "repeated init normal {}", iteration);
            Logger::report(Logger::Types::Level::Warn, testSource, "repeated init report {}", iteration);
            context.expectTrue("repeated init flush", Logger::flush(2s));
            Logger::shutdown();
            context.expectFalse("repeated init shutdown stopped", Logger::isRunning());
        }
    }

    /// @brief Launches one Logger child mode with isolated log-directory inheritance and capture.
    TestSupport::Types::ChildProcessResult runChildProcessResult(
        std::string_view executablePath,
        std::string_view argument,
        std::chrono::milliseconds timeout = 5000ms)
    {
        TestSupport::Types::ChildProcessOptions child;
        child.executablePath = std::filesystem::path(std::string(executablePath));
        child.arguments = {std::string(argument)};
        child.timeout = timeout;
        child.captureOutput = true;
        return TestSupport::runChildProcess(child);
    }

    /// @brief Executes the fatal-termination child protocol; successful behavior does not return normally.
    int runFatalTerminateChild()
    {
#if defined(_WIN32)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
        Logger::Types::Config config;
        config.output = Logger::Types::Output::None;
        config.minLevel = Logger::Types::Level::Trace;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        if (const char *childLogDirectory = std::getenv(std::string(childLogDirectoryEnvironmentVariable).c_str()))
        {
            config.output = Logger::Types::Output::File;
            config.logDirectory = childLogDirectory;
            config.fallbackToConsoleOnFileFailure = false;
            config.flushFileEveryBatch = true;
        }
        Logger::init(config);
        Logger::fatalTerminate(testSource, fatalTerminateChildMessage);
    }

    /// @brief Verifies fatalTerminate reports synchronously and terminates its child process.
    void testFatalTerminateChild(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableChildCrashTests)
        {
            context.pass("fatalTerminate child test disabled by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger fatalTerminate child test");
        const std::filesystem::path childLogDirectory = context.logRoot / "fatal_terminate_child";
        TestSupport::createDirectories(childLogDirectory);
        const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));

        std::cout.flush();
        std::cerr.flush();
        const TestSupport::Types::ChildProcessResult result = runChildProcessResult(context.executablePath, "--logger-test-child=fatal-terminate");
        context.expectTrue("fatalTerminate child exits abnormally", result.exitedWithFailure(), "child process returned zero");
        const std::string childLogContents = readDirectoryFiles(childLogDirectory);
        context.expectTrue(
            "fatalTerminate child logs fatal through report",
            childLogContents.find("[FATAL][LoggerTest]: child fatal terminate") != std::string::npos,
            "fatalTerminate child log missing");
        std::cout.flush();
    }

    /// @brief Runs the opt-in human check for Logger-owned fatal popup presentation.
    void testManualLoggerFatalPopup(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableLoggerPopupTest)
        {
            context.pass("manual logger fatal popup test skipped by LoggerTestOptions");
            return;
        }

        ScopedLoggerShutdown loggerShutdown;
        Logger::Types::Config config = makeConsoleConfig(Logger::Types::Level::Trace);
        config.output = Logger::Types::Output::Console;
        config.enableFatalPopup = true;
        config.enableDebugOutput = false;

        context.emit("[MANUAL] Logger fatal popup: a logger-owned fatal popup should appear. Close it to continue.\n");
        expectInitSuccess(context, "manual logger fatal popup init", Logger::init(config));
        Logger::report(
            Logger::Types::Level::Fatal,
            testSource,
            Logger::Types::ReportPopup::Fatal,
            "Manual logger fatal popup test. Close this popup to continue.");
        Logger::shutdown();
        context.pass("manual logger fatal popup scenario completed");
    }

    /// @brief Records final Logger/process memory peaks and aggregate suite counts in the report.
    void printEndSummary(TestContext &context)
    {
        const Logger::Types::MemoryStats finalMemory = recordMemorySnapshot(context, "suite-end");

        context.emit(
            std::format(
                "[SUMMARY] tests={} passed={} failed={}\n",
                context.result().passed + context.result().failed + context.result().skipped,
                context.result().passed,
                context.result().failed));

        if (context.loggerMemoryPeak.available)
        {
            const Logger::Types::MemoryStats &peak = context.loggerMemoryPeak.memory;
            context.emit(
                std::format(
                    "[SUMMARY] loggerMemoryPeak label={} retained={} queue={} arena={} sources={} entryHeap={} entryHeapInspectable={}\n",
                    context.loggerMemoryPeak.label,
                    formatBytes(peak.loggerRetainedBytes),
                    formatBytes(peak.queueStorageBytes),
                    formatBytes(peak.messageArenaBytes),
                    formatBytes(peak.sourceRegistryBytes),
                    formatBytes(peak.entryTextHeapCapacityBytes),
                    peak.entryTextHeapCapacityAvailable));
        }

        if (context.processMemoryPeak.available)
        {
            const Logger::Types::MemoryStats &peak = context.processMemoryPeak.memory;
            context.emit(
                std::format(
                    "[SUMMARY] processMemoryPeak label={} private={} workingSet={}\n",
                    context.processMemoryPeak.label,
                    formatBytes(peak.processPrivateBytes),
                    formatBytes(peak.processWorkingSetBytes)));
        }

        context.emit(
            std::format(
                "[SUMMARY] finalMemory loggerRetained={} processPrivate={} processWorkingSet={}\n",
                formatBytes(finalMemory.loggerRetainedBytes),
                formatProcessBytes(finalMemory, finalMemory.processPrivateBytes),
                formatProcessBytes(finalMemory, finalMemory.processWorkingSetBytes)));
    }

    /// @brief Returns whether one exact Logger child-mode argument is present.
    bool hasArgument(int argc, char **argv, std::string_view argument)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] == argument)
            {
                return true;
            }
        }
        return false;
    }
} // namespace

namespace GameWIP::Test
{
    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options)
    {
        tracy::SetThreadName("LoggerTestMain");

        if (hasArgument(argc, argv, "--logger-test-child=fatal-terminate"))
        {
            return runFatalTerminateChild();
        }

        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("Logger test report: {}", options.writeReport ? options.reportPath.string() : std::string{"disabled"}));

        const TestSupport::Types::SuiteResult suite = runner.runSuite(
            "Logger",
            [&](TestSupport::Context &suiteContext)
            {
                TestContext context(suiteContext);
                const TestSupport::ScopedTemporaryDirectory workspace("logger_tests");
                context.executablePath = argc > 0 && argv[0] != nullptr ? std::filesystem::absolute(argv[0]).string() : "";
                const TestSupport::ScopedCurrentPath temporaryCurrentPath(workspace.path());
                const ScopedLoggerShutdown loggerShutdown;
                context.logRoot = workspace.path();

                context.emit(std::format("[INFO] Logger test log root: {}\n", pathText(context.logRoot)));
                context.emit(
                    std::format(
                        "[INFO] Logger test options: stress={} fatalChild={} manualUi={} loggerPopup={} "
                        "stressThreads={} stressIterations={} report={}\n",
                        options.enableStressTests,
                        options.enableChildCrashTests,
                        options.enableManualUiTests,
                        options.enableLoggerPopupTest,
                        options.stressThreadCount,
                        options.stressIterationsPerThread,
                        options.writeReport ? options.reportPath.string() : std::string{"disabled"}));

                Logger::shutdown();
                runCase(
                    context,
                    "config factories",
                    [&]
                    {
                        testConfigFactories(context);
                    });
                runCase(
                    context,
                    "disabled and invalid init",
                    [&]
                    {
                        testDisabledAndInvalidInit(context);
                    });
                runCase(
                    context,
                    "convenience init APIs",
                    [&]
                    {
                        testConvenienceInitApis(context);
                    });
                runCase(
                    context,
                    "file output and content",
                    [&]
                    {
                        testFileOutputAndContent(context);
                    });
                runCase(
                    context,
                    "foundation file sink",
                    [&]
                    {
                        testFoundationFileSink(context);
                    });
                runCase(
                    context,
                    "Terminal console sink",
                    [&]
                    {
                        testTerminalConsoleSink(context);
                    });
                runCase(
                    context,
                    "lifecycle and queries",
                    [&]
                    {
                        testLifecycleAndQueries(context);
                    });
                runCase(
                    context,
                    "level and source filters",
                    [&]
                    {
                        testLevelAndSourceFilters(context);
                    });
                runCase(
                    context,
                    "source validation",
                    [&]
                    {
                        testSourceValidation(context);
                    });
                runCase(
                    context,
                    "formatting and truncation",
                    [&]
                    {
                        testFormattingAndTruncation(context);
                    });
                runCase(
                    context,
                    "reports and debug output",
                    [&]
                    {
                        testReportsAndDebugOutput(context);
                    });
                runCase(
                    context,
                    "report failure and unknown-source paths",
                    [&]
                    {
                        testReportFailureAndUnknownSourcePaths(context);
                    });
                runCase(
                    context,
                    "logger test hooks",
                    [&]
                    {
                        testLoggerTestHooks(context);
                    });
                runCase(
                    context,
                    "file fallback",
                    [&]
                    {
                        testFileFallback(context);
                    });
                runCase(
                    context,
                    "macro behavior",
                    [&]
                    {
                        testMacroBehavior(context);
                    });
                runCase(
                    context,
                    "filtered logs drop accounting",
                    [&]
                    {
                        testFilteredLogsDoNotCountAsDrops(context);
                    });
                runCase(
                    context,
                    "stats reset and lifetime drops",
                    [&]
                    {
                        testStatsResetKeepsLifetimeQueueDrops(context);
                    });
                runCase(
                    context,
                    "queue pressure and concurrency",
                    [&]
                    {
                        testQueuePressureAndConcurrency(context, options);
                    });
                runCase(
                    context,
                    "report bypasses full queue",
                    [&]
                    {
                        testReportBypassesFullQueue(context, options);
                    });
                runCase(
                    context,
                    "report while producers active",
                    [&]
                    {
                        testReportWhileProducersActive(context, options);
                    });
                runCase(
                    context,
                    "flush while producers active",
                    [&]
                    {
                        testFlushWhileProducersActive(context, options);
                    });
                runCase(
                    context,
                    "shutdown while producers active",
                    [&]
                    {
                        testShutdownWhileProducersActive(context, options);
                    });
                runCase(
                    context,
                    "repeated init shutdown stress",
                    [&]
                    {
                        testRepeatedInitShutdownStress(context, options);
                    });
                runCase(
                    context,
                    "fatal terminate child",
                    [&]
                    {
                        testFatalTerminateChild(context, options);
                    });
                runCase(
                    context,
                    "manual logger fatal popup",
                    [&]
                    {
                        testManualLoggerFatalPopup(context, options);
                    });
                runCase(
                    context,
                    "manual logger UI option",
                    [&]
                    {
                        if (options.enableManualUiTests)
                        {
                            context.pass("manual logger UI tests enabled; add explicit manual scenarios before running unattended");
                        }
                        else
                        {
                            context.pass("manual logger UI tests skipped by LoggerTestOptions");
                        }
                    });
                Logger::shutdown();
                printEndSummary(context);
            });

        runner.summary(
            std::format(
                "logger passed={} failed={} skipped={} elapsedMs={:.3f}",
                suite.summary.passed,
                suite.summary.failed,
                suite.summary.skipped,
                suite.elapsedMilliseconds));
        Logger::shutdown();
        return runner.exitCode();
    }

} // namespace GameWIP::Test
