#include "test/logger_test.h"

#include "logger/logger.h"
#include "logger/logger_macros.h"

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

namespace
{
    using Logger = GameWIP::Logger;
    using LoggerTestOptions = GameWIP::Test::LoggerTestOptions;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::string_view testSource = "LoggerTest";
    constexpr std::string_view shortMessage = "logger test message";

    enum class TestSource : Logger::SourceId
    {
        Core = 1,
        Render = 2,
        Audio = 3,
        Unknown = 99
    };

    struct MemoryPeak
    {
        Logger::MemoryStats memory;
        std::string label;
        bool available = false;
    };

    struct PerformanceTotals
    {
        std::size_t scenarioCount = 0;
        std::size_t measuredMessages = 0;
        double producerMilliseconds = 0.0;
        std::size_t queued = 0;
        std::size_t written = 0;
        std::size_t dropped = 0;
        std::size_t truncated = 0;
        std::size_t peakQueueDepth = 0;
    };

    struct TestContext
    {
        int passed = 0;
        int failed = 0;
        std::filesystem::path logRoot;
        std::string executablePath;
        MemoryPeak loggerMemoryPeak;
        MemoryPeak processMemoryPeak;
        PerformanceTotals performanceTotals;

        void pass(std::string_view name)
        {
            ++passed;
            std::cout << std::format("[PASS] {}\n", name);
        }

        void fail(std::string_view name, std::string_view details)
        {
            ++failed;
            std::cout << std::format("[FAIL] {}: {}\n", name, details);
        }

        template <typename Left, typename Right>
        void expectEq(std::string_view name, const Left &actual, const Right &expected)
        {
            if (actual == expected)
            {
                pass(name);
                return;
            }

            fail(name, std::format("expected {}, got {}", expected, actual));
        }

        void expectTrue(std::string_view name, bool value, std::string_view details = "expected true")
        {
            if (value)
            {
                pass(name);
                return;
            }

            fail(name, details);
        }

        void expectFalse(std::string_view name, bool value, std::string_view details = "expected false")
        {
            if (!value)
            {
                pass(name);
                return;
            }

            fail(name, details);
        }
    };

    struct ScopedLoggerShutdown
    {
        ~ScopedLoggerShutdown()
        {
            Logger::shutdown();
        }
    };

    struct Timing
    {
        double milliseconds = 0.0;
    };

    std::string_view toString(Logger::Result result)
    {
        switch (result)
        {
        case Logger::Result::Success:
            return "Success";
        case Logger::Result::AlreadyRunning:
            return "AlreadyRunning";
        case Logger::Result::InvalidOutputMode:
            return "InvalidOutputMode";
        case Logger::Result::InvalidQueueSize:
            return "InvalidQueueSize";
        case Logger::Result::InvalidMessageLength:
            return "InvalidMessageLength";
        case Logger::Result::InvalidLogDirectory:
            return "InvalidLogDirectory";
        case Logger::Result::InvalidSourceDefinition:
            return "InvalidSourceDefinition";
        case Logger::Result::InvalidSourceFilter:
            return "InvalidSourceFilter";
        case Logger::Result::InvalidLevelFilter:
            return "InvalidLevelFilter";
        case Logger::Result::FileOpenFailed:
            return "FileOpenFailed";
        case Logger::Result::FileWriteFailed:
            return "FileWriteFailed";
        case Logger::Result::FileSetupFailed:
            return "FileSetupFailed";
        case Logger::Result::ThreadStartFailed:
            return "ThreadStartFailed";
        case Logger::Result::PlatformCallFailed:
            return "PlatformCallFailed";
        }

        return "Unknown";
    }

    std::string_view toString(Logger::Output output)
    {
        switch (output)
        {
        case Logger::Output::None:
            return "None";
        case Logger::Output::Console:
            return "Console";
        case Logger::Output::File:
            return "File";
        case Logger::Output::Both:
            return "Both";
        }

        return "Unknown";
    }

    std::string_view toString(Logger::Level level)
    {
        switch (level)
        {
        case Logger::Level::Trace:
            return "Trace";
        case Logger::Level::Debug:
            return "Debug";
        case Logger::Level::Info:
            return "Info";
        case Logger::Level::Warn:
            return "Warn";
        case Logger::Level::Error:
            return "Error";
        case Logger::Level::Fatal:
            return "Fatal";
        }

        return "Unknown";
    }

    template <typename Value>
    std::string printable(Value value)
    {
        if constexpr (std::is_same_v<Value, Logger::Result>)
        {
            return std::string(toString(value));
        }
        else if constexpr (std::is_same_v<Value, Logger::Output>)
        {
            return std::string(toString(value));
        }
        else if constexpr (std::is_same_v<Value, Logger::Level>)
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

    template <typename Left, typename Right>
    void expectEq(TestContext &context, std::string_view name, const Left &actual, const Right &expected)
    {
        if (actual == expected)
        {
            context.pass(name);
            return;
        }

        context.fail(name, std::format("expected {}, got {}", printable(expected), printable(actual)));
    }

    template <typename Function>
    Timing measure(Function function)
    {
        const auto start = Clock::now();
        function();
        const auto end = Clock::now();
        return Timing{std::chrono::duration<double, std::milli>(end - start).count()};
    }

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

    std::string formatProcessBytes(const Logger::MemoryStats &memory, std::size_t bytes)
    {
        return memory.processMemoryAvailable ? formatBytes(bytes) : "n/a";
    }

    Logger::MemoryStats recordMemorySnapshot(TestContext &context, std::string_view label)
    {
        const Logger::MemoryStats memory = Logger::getMemoryStats();
        if (!context.loggerMemoryPeak.available ||
            memory.loggerRetainedBytes > context.loggerMemoryPeak.memory.loggerRetainedBytes)
        {
            context.loggerMemoryPeak.memory = memory;
            context.loggerMemoryPeak.label = std::string(label);
            context.loggerMemoryPeak.available = true;
        }

        if (memory.processMemoryAvailable &&
            (!context.processMemoryPeak.available ||
             memory.processPrivateBytes > context.processMemoryPeak.memory.processPrivateBytes))
        {
            context.processMemoryPeak.memory = memory;
            context.processMemoryPeak.label = std::string(label);
            context.processMemoryPeak.available = true;
        }

        return memory;
    }

    std::size_t totalDropped(const Logger::Stats &stats)
    {
        return stats.droppedSoft + stats.droppedHard + stats.droppedAllocation + stats.droppedFiltered + stats.formatFailures;
    }

    std::filesystem::path makeRunRoot()
    {
        const auto now = std::chrono::system_clock::now().time_since_epoch().count();
        const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        return std::filesystem::temp_directory_path() / "GameWIPLoggerTests" / std::format("run_{}_{}", now, threadHash);
    }

    std::string pathText(const std::filesystem::path &path)
    {
        return path.generic_string();
    }

    std::filesystem::path testDirectory(TestContext &context, std::string_view name)
    {
        std::filesystem::path directory = context.logRoot / std::string(name);
        std::filesystem::create_directories(directory);
        return directory;
    }

    std::string readWholeFile(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

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

    struct OwnedLoggerConfig : Logger::Config
    {
        std::string ownedLogDirectory;

        const Logger::Config &ready()
        {
            logDirectory = ownedLogDirectory;
            return *this;
        }
    };

    OwnedLoggerConfig makeConfig(Logger::Output output, Logger::Level minLevel, const std::filesystem::path &directory)
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

    OwnedLoggerConfig makeFileConfig(TestContext &context, std::string_view name, Logger::Level minLevel = Logger::Level::Trace)
    {
        return makeConfig(Logger::Output::File, minLevel, testDirectory(context, name));
    }

    Logger::Config makeConsoleConfig(Logger::Level minLevel = Logger::Level::Trace)
    {
        Logger::Config config;
        config.output = Logger::Output::Console;
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

    void expectInitSuccess(TestContext &context, std::string_view name, Logger::Result result)
    {
        expectEq(context, name, result, Logger::Result::Success);
        if (result != Logger::Result::Success)
        {
            const Logger::PlatformError platformError = Logger::getLastPlatformError();
            context.fail(
                std::format("{} platform error", name),
                std::format("source={} native={}", static_cast<int>(platformError.source), platformError.nativeCode));
        }
    }

    std::string makeLazyArgument(std::atomic<int> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
        return "lazy argument evaluated";
    }

    void testConfigFactories(TestContext &context)
    {
        ZoneScopedN("Logger config factory tests");

        const Logger::Config defaults = Logger::defaultConfig();
        expectEq(context, "defaultConfig output", defaults.output, Logger::Output::Both);
        expectEq(context, "defaultConfig min level", defaults.minLevel, Logger::Level::Info);
        context.expectTrue("defaultConfig queue positive", defaults.maxQueueSize > 0);
        context.expectTrue("defaultConfig message positive", defaults.maxMessageLength > 0);

        const Logger::Config lowMemory = Logger::lowMemoryConfig();
        context.expectTrue("lowMemoryConfig lower queue", lowMemory.maxQueueSize < defaults.maxQueueSize);
        context.expectTrue("lowMemoryConfig lower message length", lowMemory.maxMessageLength < defaults.maxMessageLength);

        const Logger::Config throughput = Logger::throughputConfig();
        context.expectTrue("throughputConfig higher queue", throughput.maxQueueSize > defaults.maxQueueSize);
        context.expectFalse("throughputConfig retains storage", throughput.releaseStorageOnShutdown);
    }

    void testDisabledAndInvalidInit(TestContext &context)
    {
        ZoneScopedN("Logger disabled and invalid init tests");
        ScopedLoggerShutdown shutdown;

        Logger::Config disabled = makeConsoleConfig(Logger::Level::Trace);
        disabled.output = Logger::Output::None;
        const Logger::Result disabledResult = Logger::init(disabled);
        expectEq(context, "init output none succeeds", disabledResult, Logger::Result::Success);
        context.expectFalse("output none is not running", Logger::isRunning());
        context.expectFalse("output none shouldLog false", Logger::shouldLog(Logger::Level::Info));
        Logger::info(testSource, "not accepted");
        const Logger::Stats disabledStats = Logger::getStats();
        expectEq(context, "output none queued zero", disabledStats.queued, std::size_t{0});
        expectEq(context, "output none written zero", disabledStats.written, std::size_t{0});
        Logger::shutdown();

        Logger::Config invalidOutput = makeConsoleConfig();
        invalidOutput.output = static_cast<Logger::Output>(99);
        expectEq(context, "invalid output rejected", Logger::init(invalidOutput), Logger::Result::InvalidOutputMode);
        context.expectFalse("invalid output not running", Logger::isRunning());
        Logger::shutdown();

        Logger::Config invalidLevel = makeConsoleConfig();
        invalidLevel.minLevel = static_cast<Logger::Level>(99);
        expectEq(context, "invalid min level rejected", Logger::init(invalidLevel), Logger::Result::InvalidLevelFilter);
        Logger::shutdown();

        Logger::Config invalidLevelFilter = makeConsoleConfig();
        std::array invalidLevelFilters{Logger::LevelFilter{static_cast<Logger::Level>(99), true}};
        invalidLevelFilter.levelFilters = invalidLevelFilters;
        expectEq(context, "invalid level filter rejected", Logger::init(invalidLevelFilter), Logger::Result::InvalidLevelFilter);
        Logger::shutdown();

        Logger::Config duplicateLevelFilter = makeConsoleConfig();
        std::array duplicateLevelFilters{
            Logger::LevelFilter{Logger::Level::Info, false},
            Logger::LevelFilter{Logger::Level::Info, true}};
        duplicateLevelFilter.levelFilters = duplicateLevelFilters;
        expectEq(context, "duplicate level filter rejected", Logger::init(duplicateLevelFilter), Logger::Result::InvalidLevelFilter);
        Logger::shutdown();

        Logger::Config invalidQueue = makeConsoleConfig();
        invalidQueue.maxQueueSize = 0;
        const Logger::Result invalidQueueResult = Logger::init(invalidQueue);
        expectEq(context, "zero queue sanitized result", invalidQueueResult, Logger::Result::InvalidQueueSize);
        context.expectTrue("zero queue still starts sanitized logger", Logger::isRunning());
        Logger::shutdown();

        Logger::Config invalidMessageLength = makeConsoleConfig();
        invalidMessageLength.maxMessageLength = 0;
        const Logger::Result invalidMessageResult = Logger::init(invalidMessageLength);
        expectEq(context, "zero message length sanitized result", invalidMessageResult, Logger::Result::InvalidMessageLength);
        context.expectTrue("zero message length still starts sanitized logger", Logger::isRunning());
    }

    void testConvenienceInitApis(TestContext &context)
    {
        ZoneScopedN("Logger convenience init API tests");
        ScopedLoggerShutdown shutdown;

        expectEq(context, "initConsole succeeds", Logger::initConsole(Logger::Level::Warn), Logger::Result::Success);
        expectEq(context, "initConsole output", Logger::getOutput(), Logger::Output::Console);
        expectEq(context, "initConsole min level", Logger::getMinLevel(), Logger::Level::Warn);
        context.expectFalse("initConsole filters info", Logger::shouldLog(Logger::Level::Info));
        context.expectTrue("initConsole allows error", Logger::shouldLog(Logger::Level::Error));
        Logger::shutdown();

        const std::string initFileDirectory = pathText(testDirectory(context, "init-file-convenience"));
        expectInitSuccess(context, "initFile explicit directory succeeds", Logger::initFile(initFileDirectory, Logger::Level::Info));
        Logger::info(testSource, "initFile visible");
        context.expectTrue("initFile flush succeeds", Logger::flush(2s));
        const std::string initFilePath = Logger::getLogFilePath();
        context.expectFalse("initFile path available", initFilePath.empty());
        Logger::shutdown();
        context.expectTrue("initFile wrote content", readWholeFile(initFilePath).find("initFile visible") != std::string::npos);

        const Logger::Result defaultResult = Logger::initDefault();
        const bool defaultStarted =
            defaultResult == Logger::Result::Success ||
            defaultResult == Logger::Result::FileOpenFailed ||
            defaultResult == Logger::Result::FileSetupFailed;
        context.expectTrue("initDefault starts or falls back", defaultStarted);
        context.expectTrue("initDefault leaves logger running", Logger::isRunning());
        if (defaultResult == Logger::Result::Success)
        {
            expectEq(context, "initDefault output", Logger::getOutput(), Logger::Output::Both);
            context.expectFalse("initDefault path available", Logger::getLogFilePath().empty());
        }
        else
        {
            expectEq(context, "initDefault file failure falls back to console", Logger::getOutput(), Logger::Output::Console);
        }
    }

    void testFileOutputAndContent(TestContext &context)
    {
        ZoneScopedN("Logger file output tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "file-output");
        config.maxQueueSize = 256;
        config.maxMessageLength = 128;
        config.flushFileEveryBatch = true;
        const Logger::Result result = Logger::init(config.ready());
        expectInitSuccess(context, "file init succeeds", result);
        context.expectTrue("file logger running", Logger::isRunning());
        expectEq(context, "file logger output mode", Logger::getOutput(), Logger::Output::File);
        expectEq(context, "file logger min level", Logger::getMinLevel(), Logger::Level::Trace);

        Logger::trace(testSource, "trace line");
        Logger::debug(testSource, "debug {}", 7);
        Logger::info(testSource, Logger::runtimeFormat("runtime {}"), 11);
        Logger::warn(testSource, "warn line");
        Logger::error(testSource, "error line");
        Logger::fatal(testSource, "fatal line");
        context.expectTrue("file flush succeeds", Logger::flush(2s));

        const std::string logFile = Logger::getLogFilePath();
        context.expectFalse("file path available before shutdown", logFile.empty());
        const Logger::Stats stats = Logger::getStats();
        expectEq(context, "file output queued all levels", stats.queued, std::size_t{6});
        expectEq(context, "file output wrote all levels", stats.written, std::size_t{6});
        expectEq(context, "file output no drops", totalDropped(stats), std::size_t{0});
        Logger::shutdown();

        const std::string contents = readWholeFile(logFile);
        context.expectTrue("file contains trace", contents.find("[TRACE][LoggerTest]: trace line") != std::string::npos);
        context.expectTrue("file contains formatted debug", contents.find("[DEBUG][LoggerTest]: debug 7") != std::string::npos);
        context.expectTrue("file contains runtime format", contents.find("[INFO][LoggerTest]: runtime 11") != std::string::npos);
        context.expectTrue("file contains fatal", contents.find("[FATAL][LoggerTest]: fatal line") != std::string::npos);
    }

    void testLifecycleAndQueries(TestContext &context)
    {
        ZoneScopedN("Logger lifecycle and query tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "lifecycle", Logger::Level::Info);
        config.maxQueueSize = 32;
        config.hardQueueMultiplier = 2.0;
        config.maxMessageLength = 96;
        config.inlineMessageCapacity = 32;
        config.workerBatchSize = 8;
        expectInitSuccess(context, "lifecycle init", Logger::init(config.ready()));

        const Logger::QueueLimits limits = Logger::getQueueLimits();
        expectEq(context, "queue soft limit", limits.softQueueSize, std::size_t{32});
        expectEq(context, "queue hard limit", limits.hardQueueSize, std::size_t{64});
        expectEq(context, "queue max message length", limits.maxMessageLength, std::size_t{96});
        expectEq(context, "queue inline capacity", limits.inlineMessageCapacity, std::size_t{32});
        expectEq(context, "queue worker batch size", limits.workerBatchSize, std::size_t{8});

        expectEq(context, "already running rejected", Logger::init(config.ready()), Logger::Result::AlreadyRunning);
        context.expectTrue("last result already running", Logger::getLastResult() == Logger::Result::AlreadyRunning);
        Logger::resetStats();
        const Logger::Stats resetStats = Logger::getStats();
        expectEq(context, "reset stats queued zero", resetStats.queued, std::size_t{0});

        const Logger::MemoryStats memory = Logger::getMemoryStats();
        context.expectTrue("memory stats retained bytes", memory.loggerRetainedBytes > 0);
        context.expectTrue("memory stats queue bytes", memory.queueStorageBytes > 0);
        context.expectTrue("memory stats message arena bytes", memory.messageArenaBytes > 0);

        Logger::shutdown();
        context.expectFalse("shutdown clears running", Logger::isRunning());
        expectEq(context, "shutdown output none", Logger::getOutput(), Logger::Output::None);
        expectEq(context, "shutdown clears log path", Logger::getLogFilePath().size(), std::size_t{0});
    }

    void testLevelAndSourceFilters(TestContext &context)
    {
        ZoneScopedN("Logger level and source filter tests");
        ScopedLoggerShutdown shutdown;

        std::array sources{
            Logger::defineSource(TestSource::Core, "Core"),
            Logger::defineSource(TestSource::Render, "Render"),
            Logger::defineSource(TestSource::Audio, "Audio")};
        std::array sourceFilters{
            Logger::SourceFilter{static_cast<Logger::SourceId>(TestSource::Render), false}};
        std::array levelFilters{
            Logger::LevelFilter{Logger::Level::Debug, false}};

        OwnedLoggerConfig config = makeFileConfig(context, "filters", Logger::Level::Trace);
        config.sources = sources;
        config.sourceFilters = sourceFilters;
        config.levelFilters = levelFilters;
        expectInitSuccess(context, "filter init", Logger::init(config.ready()));

        context.expectTrue("core source allowed", Logger::shouldLog(Logger::Level::Info, TestSource::Core));
        context.expectFalse("render source initially filtered", Logger::shouldLog(Logger::Level::Info, TestSource::Render));
        context.expectFalse("debug level initially filtered", Logger::shouldLog(Logger::Level::Debug));

        Logger::debug(TestSource::Core, "filtered debug");
        Logger::info(TestSource::Render, "filtered render");
        Logger::info(TestSource::Core, "core visible");
        Logger::info(TestSource::Unknown, "unknown source visible");
        context.expectTrue("filter flush", Logger::flush(2s));

        Logger::Stats stats = Logger::getStats();
        expectEq(context, "filter queued accepted entries", stats.queued, std::size_t{2});
        context.expectTrue("filter counted filtered drops", stats.droppedFiltered >= 2);
        expectEq(context, "unknown source counted", stats.unknownSourceUses, std::size_t{1});

        expectEq(context, "set source filter succeeds", Logger::setSourceFilter(TestSource::Render, true), Logger::Result::Success);
        expectEq(context, "clear source filter succeeds", Logger::clearSourceFilter(TestSource::Render), Logger::Result::Success);
        expectEq(context, "unknown source filter rejected", Logger::setSourceFilter(TestSource::Unknown, false), Logger::Result::InvalidSourceFilter);
        Logger::clearSourceFilters();
        context.expectTrue("render source allowed after clear", Logger::shouldLog(Logger::Level::Info, TestSource::Render));

        expectEq(context, "set level filter succeeds", Logger::setLevelFilter(Logger::Level::Info, false), Logger::Result::Success);
        context.expectFalse("info level filtered", Logger::shouldLog(Logger::Level::Info));
        Logger::info(TestSource::Core, "filtered info");
        expectEq(context, "clear level filter succeeds", Logger::clearLevelFilter(Logger::Level::Info), Logger::Result::Success);
        Logger::clearLevelFilters();
        context.expectTrue("debug level allowed after clear", Logger::shouldLog(Logger::Level::Debug));
        expectEq(context, "invalid runtime level filter rejected", Logger::setLevelFilter(static_cast<Logger::Level>(99), true), Logger::Result::InvalidLevelFilter);
    }

    void testSourceValidation(TestContext &context)
    {
        ZoneScopedN("Logger source validation tests");
        ScopedLoggerShutdown shutdown;

        std::array emptyNameSources{Logger::SourceDefinition{1, {}}};
        Logger::Config emptyNameConfig = makeConsoleConfig();
        emptyNameConfig.sources = emptyNameSources;
        expectEq(context, "empty source name rejected", Logger::init(emptyNameConfig), Logger::Result::InvalidSourceDefinition);
        Logger::shutdown();

        std::array duplicateSources{
            Logger::SourceDefinition{1, "Core"},
            Logger::SourceDefinition{1, "CoreDuplicate"}};
        Logger::Config duplicateConfig = makeConsoleConfig();
        duplicateConfig.sources = duplicateSources;
        expectEq(context, "duplicate source rejected", Logger::init(duplicateConfig), Logger::Result::InvalidSourceDefinition);
        Logger::shutdown();

        std::array validSources{Logger::SourceDefinition{1, "Core"}};
        std::array invalidFilters{Logger::SourceFilter{99, false}};
        Logger::Config invalidFilterConfig = makeConsoleConfig();
        invalidFilterConfig.sources = validSources;
        invalidFilterConfig.sourceFilters = invalidFilters;
        expectEq(context, "unknown source filter rejected at init", Logger::init(invalidFilterConfig), Logger::Result::InvalidSourceFilter);
        Logger::shutdown();

        std::array duplicateFilters{
            Logger::SourceFilter{1, false},
            Logger::SourceFilter{1, true}};
        Logger::Config duplicateFilterConfig = makeConsoleConfig();
        duplicateFilterConfig.sources = validSources;
        duplicateFilterConfig.sourceFilters = duplicateFilters;
        expectEq(context, "duplicate source filter rejected at init", Logger::init(duplicateFilterConfig), Logger::Result::InvalidSourceFilter);
    }

    void testFormattingAndTruncation(TestContext &context)
    {
        ZoneScopedN("Logger formatting and truncation tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig strictConfig = makeFileConfig(context, "format-strict", Logger::Level::Trace);
        strictConfig.maxMessageLength = 48;
        strictConfig.inlineMessageCapacity = 16;
        strictConfig.formatPolicy = Logger::FormatPolicy::StrictBounded;
        expectInitSuccess(context, "strict format init", Logger::init(strictConfig.ready()));

        Logger::info(testSource, "value {} {}", 12, "ok");
        Logger::info(testSource, Logger::runtimeFormat("runtime {} {}"), 13, "ok");
        Logger::info(testSource, Logger::runtimeFormat("{"), 1);
        Logger::info(testSource, "long {}", std::string(256, 'x'));
        context.expectTrue("strict format flush", Logger::flush(2s));
        Logger::Stats strictStats = Logger::getStats();
        expectEq(context, "strict format queued", strictStats.queued, std::size_t{3});
        expectEq(context, "strict runtime format failure counted", strictStats.formatFailures, std::size_t{1});
        expectEq(context, "strict truncation counted", strictStats.truncated, std::size_t{1});
        const std::string strictPath = Logger::getLogFilePath();
        Logger::shutdown();

        const std::string strictContents = readWholeFile(strictPath);
        context.expectTrue("strict compile format content", strictContents.find("value 12 ok") != std::string::npos);
        context.expectTrue("strict runtime format content", strictContents.find("runtime 13 ok") != std::string::npos);
        context.expectTrue("strict truncation suffix content", strictContents.find("[truncated]") != std::string::npos);

        OwnedLoggerConfig fastConfig = makeFileConfig(context, "format-fast", Logger::Level::Trace);
        fastConfig.maxMessageLength = 48;
        fastConfig.formatPolicy = Logger::FormatPolicy::FastNormal;
        expectInitSuccess(context, "fast format init", Logger::init(fastConfig.ready()));
        Logger::info(testSource, "fast {}", std::string(256, 'y'));
        context.expectTrue("fast format flush", Logger::flush(2s));
        const Logger::Stats fastStats = Logger::getStats();
        expectEq(context, "fast truncation counted", fastStats.truncated, std::size_t{1});
    }

    void testReportsAndDebugOutput(TestContext &context)
    {
        ZoneScopedN("Logger report and debug output tests");
        ScopedLoggerShutdown shutdown;

        std::array sources{Logger::defineSource(TestSource::Core, "Core")};
        OwnedLoggerConfig config = makeFileConfig(context, "reports", Logger::Level::Fatal);
        config.sources = sources;
        config.enableDebugOutput = true;
        config.enableFatalPopup = false;
        expectInitSuccess(context, "report init", Logger::init(config.ready()));

        context.expectFalse("normal info below min", Logger::shouldLog(Logger::Level::Info));
        Logger::reportError(testSource, "plain report");
        context.expectTrue("timeout report plain", Logger::reportError(testSource, Logger::flushTimeout(2s), "timeout report"));
        Logger::reportError(testSource, "formatted report {}", 21);
        context.expectTrue("source runtime report", Logger::reportFatal(TestSource::Core, Logger::flushTimeout(2s), Logger::runtimeFormat("runtime fatal {}"), 22));
        Logger::writeDebugOutput(Logger::Level::Error, testSource, "debug output direct");
        context.expectTrue("report flush", Logger::flush(2s));

        const Logger::Stats stats = Logger::getStats();
        expectEq(context, "reports bypass min queued", stats.queued, std::size_t{4});
        expectEq(context, "reports written", stats.written, std::size_t{4});
        const std::string logFile = Logger::getLogFilePath();
        Logger::shutdown();

        const std::string contents = readWholeFile(logFile);
        context.expectTrue("report file plain", contents.find("plain report") != std::string::npos);
        context.expectTrue("report file formatted", contents.find("formatted report 21") != std::string::npos);
        context.expectTrue("report file runtime source", contents.find("[FATAL][Core]: runtime fatal 22") != std::string::npos);
    }

    void testFileFallback(TestContext &context)
    {
        ZoneScopedN("Logger file fallback tests");
        ScopedLoggerShutdown shutdown;

        const std::filesystem::path blockingFile = context.logRoot / "not-a-directory";
        {
            std::ofstream file(blockingFile);
            file << "blocks directory creation";
        }

        OwnedLoggerConfig noFallback = makeConfig(Logger::Output::File, Logger::Level::Info, blockingFile);
        noFallback.fallbackToConsoleOnFileFailure = false;
        const Logger::Result noFallbackResult = Logger::init(noFallback.ready());
        context.expectTrue(
            "file setup failure without fallback reported",
            noFallbackResult == Logger::Result::FileSetupFailed || noFallbackResult == Logger::Result::InvalidLogDirectory);
        expectEq(context, "file setup failure output none", Logger::getOutput(), Logger::Output::None);
        Logger::shutdown();

        OwnedLoggerConfig withFallback = makeConfig(Logger::Output::File, Logger::Level::Fatal, blockingFile);
        withFallback.fallbackToConsoleOnFileFailure = true;
        const Logger::Result fallbackResult = Logger::init(withFallback.ready());
        context.expectTrue(
            "file setup failure with fallback reported",
            fallbackResult == Logger::Result::FileSetupFailed || fallbackResult == Logger::Result::InvalidLogDirectory);
        expectEq(context, "file setup fallback to console", Logger::getOutput(), Logger::Output::Console);
    }

    void testMacroBehavior(TestContext &context)
    {
        ZoneScopedN("Logger macro behavior tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "macros", Logger::Level::Fatal);
        expectInitSuccess(context, "macro init", Logger::init(config.ready()));

        std::atomic<int> sideEffects{0};
        LOGGER_INFO(testSource, "filtered {}", makeLazyArgument(sideEffects));
        LOGGER_WARN(testSource, "filtered {}", makeLazyArgument(sideEffects));
        LOGGER_ERROR(testSource, "filtered {}", makeLazyArgument(sideEffects));
        expectEq(context, "macro filters avoid arg evaluation", sideEffects.load(std::memory_order_relaxed), 0);

        LOGGER_FATAL(testSource, "accepted {}", 1);
        context.expectTrue("macro fatal flush", Logger::flush(2s));
        const Logger::Stats stats = Logger::getStats();
        expectEq(context, "macro fatal queued", stats.queued, std::size_t{1});

        std::atomic<int> debugSideEffects{0};
        LOGGER_DEBUG(testSource, "debug maybe compiled {}", makeLazyArgument(debugSideEffects));
#if !defined(NDEBUG) || defined(LOGGER_ENABLE_DEBUG_LOGS)
        expectEq(context, "debug macro active but runtime filtered", debugSideEffects.load(std::memory_order_relaxed), 0);
#else
        expectEq(context, "debug macro compiled out", debugSideEffects.load(std::memory_order_relaxed), 0);
#endif
    }

    void testQueuePressureAndConcurrency(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.pass("stress tests disabled by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger pressure and concurrency tests");
        ScopedLoggerShutdown shutdown;
        const int stressThreads = std::max(1, options.stressThreadCount);
        const int stressIterations = std::max(1, options.stressIterationsPerThread);

        {
            OwnedLoggerConfig config = makeFileConfig(context, "soft-pressure", Logger::Level::Trace);
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
            const Logger::Stats stats = Logger::getStats();
            context.expectTrue("soft pressure queued something", stats.queued > 0);
            context.expectTrue("soft pressure dropped low priority", stats.droppedSoft > 0);
            recordMemorySnapshot(context, "soft-pressure");
            Logger::shutdown();
        }

        {
            OwnedLoggerConfig config = makeFileConfig(context, "hard-pressure", Logger::Level::Trace);
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
            const Logger::Stats stats = Logger::getStats();
            context.expectTrue("hard pressure queued something", stats.queued > 0);
            context.expectTrue("hard pressure dropped at hard limit", stats.droppedHard > 0);
            recordMemorySnapshot(context, "hard-pressure");
            Logger::shutdown();
        }

        {
            OwnedLoggerConfig config = makeFileConfig(context, "shutdown-race", Logger::Level::Info);
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

    void testStatsResetAndLifetimeDrops(TestContext &context)
    {
        ZoneScopedN("Logger stats reset tests");
        ScopedLoggerShutdown shutdown;

        OwnedLoggerConfig config = makeFileConfig(context, "stats", Logger::Level::Trace);
        expectInitSuccess(context, "stats init", Logger::init(config.ready()));
        expectEq(context, "disable info level", Logger::setLevelFilter(Logger::Level::Info, false), Logger::Result::Success);
        Logger::info(testSource, "filtered info");
        const Logger::Stats beforeReset = Logger::getStats();
        context.expectTrue("stats dropped filtered before reset", beforeReset.droppedFiltered > 0);
        const std::size_t lifetimeBeforeReset = Logger::getLifetimeDroppedLogCount();
        context.expectTrue("lifetime drops before reset", lifetimeBeforeReset > 0);

        Logger::resetStats();
        const Logger::Stats afterReset = Logger::getStats();
        expectEq(context, "reset clears filtered drops", afterReset.droppedFiltered, std::size_t{0});
        expectEq(context, "reset keeps lifetime drops", Logger::getLifetimeDroppedLogCount(), lifetimeBeforeReset);
    }

    std::string quoteCommandArg(std::string_view text)
    {
        std::string quoted;
        quoted.reserve(text.size() + 2);
        quoted.push_back('"');
        for (char ch : text)
        {
            if (ch == '"')
            {
                quoted.push_back('\\');
            }
            quoted.push_back(ch);
        }
        quoted.push_back('"');
        return quoted;
    }

    int runFatalTerminateChild()
    {
        Logger::Config config;
        config.output = Logger::Output::None;
        config.minLevel = Logger::Level::Trace;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        Logger::init(config);
        Logger::fatalTerminate(testSource, "child fatal terminate");
    }

    void testFatalTerminateChild(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableFatalTerminateChildTest)
        {
            context.pass("fatalTerminate child test disabled by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger fatalTerminate child test");
        std::string command = quoteCommandArg(context.executablePath) + " --logger-test-child=fatal-terminate";
#if defined(_WIN32)
        command += " > NUL 2> NUL";
#else
        command += " > /dev/null 2> /dev/null";
#endif
        std::cout.flush();
        std::cerr.flush();
        const int result = std::system(command.c_str());
        context.expectTrue("fatalTerminate child exits abnormally", result != 0, "child process returned zero");
        std::cout.flush();
    }

    void printMetric(TestContext &context, std::string_view name, std::size_t iterations, double milliseconds, const Logger::Stats &stats)
    {
        const double nanosecondsPerCall = iterations == 0 ? 0.0 : (milliseconds * 1'000'000.0) / static_cast<double>(iterations);
        const std::size_t dropped = totalDropped(stats);
        const Logger::MemoryStats memory = recordMemorySnapshot(context, name);
        ++context.performanceTotals.scenarioCount;
        context.performanceTotals.measuredMessages += iterations;
        context.performanceTotals.producerMilliseconds += milliseconds;
        context.performanceTotals.queued += stats.queued;
        context.performanceTotals.written += stats.written;
        context.performanceTotals.dropped += dropped;
        context.performanceTotals.truncated += stats.truncated;
        context.performanceTotals.peakQueueDepth = std::max(context.performanceTotals.peakQueueDepth, stats.peakQueueDepth);

        std::cout << std::format(
            "[METRIC] {} iterations={} ms={:.3f} nsPerCall={:.2f} queued={} written={} dropped={} truncated={} peak={} loggerRetained={} processPrivate={} processWorkingSet={}\n",
            name,
            iterations,
            milliseconds,
            nanosecondsPerCall,
            stats.queued,
            stats.written,
            dropped,
            stats.truncated,
            stats.peakQueueDepth,
            formatBytes(memory.loggerRetainedBytes),
            formatProcessBytes(memory, memory.processPrivateBytes),
            formatProcessBytes(memory, memory.processWorkingSetBytes));
        TracyPlot("Logger test metric ms", milliseconds);
        TracyPlot("Logger test metric ns/call", nanosecondsPerCall);
        TracyPlot("Logger test memory retained bytes", static_cast<double>(memory.loggerRetainedBytes));
        if (memory.processMemoryAvailable)
        {
            TracyPlot("Logger test process private bytes", static_cast<double>(memory.processPrivateBytes));
            TracyPlot("Logger test process working set bytes", static_cast<double>(memory.processWorkingSetBytes));
        }
    }

    void runPerformanceMetrics(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enablePerformanceMetrics)
        {
            context.pass("performance metrics disabled by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger performance metrics");
        ScopedLoggerShutdown shutdown;

        const std::size_t iterations = std::max<std::size_t>(1, options.performanceIterations);

        {
            Logger::Config config = makeConsoleConfig(Logger::Level::Trace);
            config.output = Logger::Output::None;
            expectEq(context, "perf disabled init", Logger::init(config), Logger::Result::Success);
            const Timing timing = measure(
                [iterations]
                {
                    for (std::size_t i = 0; i < iterations; ++i)
                    {
                        Logger::info(testSource, shortMessage);
                    }
                });
            printMetric(context, "disabled-output-none", iterations, timing.milliseconds, Logger::getStats());
            Logger::shutdown();
        }

        {
            OwnedLoggerConfig config = makeFileConfig(context, "perf-filtered", Logger::Level::Fatal);
            expectInitSuccess(context, "perf filtered init", Logger::init(config.ready()));
            const Timing timing = measure(
                [iterations]
                {
                    for (std::size_t i = 0; i < iterations; ++i)
                    {
                        Logger::info(testSource, "filtered {} {}", i, i + 1);
                    }
                });
            printMetric(context, "below-min-formatted", iterations, timing.milliseconds, Logger::getStats());
            Logger::shutdown();
        }

        {
            const std::size_t enabledIterations = std::max<std::size_t>(1000, iterations / 10);
            OwnedLoggerConfig config = makeFileConfig(context, "perf-enabled", Logger::Level::Info);
            config.maxQueueSize = enabledIterations + 1024;
            config.hardQueueMultiplier = 1.0;
            config.workerBatchSize = 256;
            config.releaseMessageMemoryAfterWrite = false;
            expectInitSuccess(context, "perf enabled init", Logger::init(config.ready()));
            const Timing timing = measure(
                [enabledIterations]
                {
                    for (std::size_t i = 0; i < enabledIterations; ++i)
                    {
                        Logger::info(testSource, shortMessage);
                    }
                });
            context.expectTrue("perf enabled flush", Logger::flush(5s));
            printMetric(context, "enabled-file-preformatted", enabledIterations, timing.milliseconds, Logger::getStats());
            Logger::shutdown();
        }
    }

    void printEndSummary(TestContext &context, double suiteMilliseconds)
    {
        const Logger::MemoryStats finalMemory = recordMemorySnapshot(context, "suite-end");
        const PerformanceTotals &totals = context.performanceTotals;
        const double totalNsPerMessage = totals.measuredMessages == 0
                                             ? 0.0
                                             : (totals.producerMilliseconds * 1'000'000.0) / static_cast<double>(totals.measuredMessages);

        std::cout << std::format(
            "[SUMMARY] suiteTimeMs={:.3f} tests={} passed={} failed={}\n",
            suiteMilliseconds,
            context.passed + context.failed,
            context.passed,
            context.failed);

        std::cout << std::format(
            "[SUMMARY] perfTotals scenarios={} measuredMessages={} producerMs={:.3f} nsPerMessage={:.2f} queued={} written={} dropped={} truncated={} peakQueue={}\n",
            totals.scenarioCount,
            totals.measuredMessages,
            totals.producerMilliseconds,
            totalNsPerMessage,
            totals.queued,
            totals.written,
            totals.dropped,
            totals.truncated,
            totals.peakQueueDepth);

        if (context.loggerMemoryPeak.available)
        {
            const Logger::MemoryStats &peak = context.loggerMemoryPeak.memory;
            std::cout << std::format(
                "[SUMMARY] loggerMemoryPeak label={} retained={} queue={} arena={} sources={} entryHeap={} entryHeapInspectable={}\n",
                context.loggerMemoryPeak.label,
                formatBytes(peak.loggerRetainedBytes),
                formatBytes(peak.queueStorageBytes),
                formatBytes(peak.messageArenaBytes),
                formatBytes(peak.sourceRegistryBytes),
                formatBytes(peak.entryTextHeapCapacityBytes),
                peak.entryTextHeapCapacityAvailable);
        }

        if (context.processMemoryPeak.available)
        {
            const Logger::MemoryStats &peak = context.processMemoryPeak.memory;
            std::cout << std::format(
                "[SUMMARY] processMemoryPeak label={} private={} workingSet={}\n",
                context.processMemoryPeak.label,
                formatBytes(peak.processPrivateBytes),
                formatBytes(peak.processWorkingSetBytes));
        }

        std::cout << std::format(
            "[SUMMARY] finalMemory loggerRetained={} processPrivate={} processWorkingSet={}\n",
            formatBytes(finalMemory.loggerRetainedBytes),
            formatProcessBytes(finalMemory, finalMemory.processPrivateBytes),
            formatProcessBytes(finalMemory, finalMemory.processWorkingSetBytes));
    }

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
}

namespace GameWIP::Test
{
    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options)
    {
        tracy::SetThreadName("LoggerTestMain");

        if (hasArgument(argc, argv, "--logger-test-child=fatal-terminate"))
        {
            return runFatalTerminateChild();
        }

        const auto suiteStart = Clock::now();
        TestContext context;
        context.executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : "";
        context.logRoot = makeRunRoot();
        std::filesystem::create_directories(context.logRoot);

        std::cout << std::format("[INFO] Logger test log root: {}\n", pathText(context.logRoot));
        std::cout << std::format(
            "[INFO] Logger test options: stress={} fatalChild={} performance={} perfIterations={} stressThreads={} stressIterations={}\n",
            options.enableStressTests,
            options.enableFatalTerminateChildTest,
            options.enablePerformanceMetrics,
            options.performanceIterations,
            options.stressThreadCount,
            options.stressIterationsPerThread);

        try
        {
            Logger::shutdown();
            testConfigFactories(context);
            testDisabledAndInvalidInit(context);
            testConvenienceInitApis(context);
            testFileOutputAndContent(context);
            testLifecycleAndQueries(context);
            testLevelAndSourceFilters(context);
            testSourceValidation(context);
            testFormattingAndTruncation(context);
            testReportsAndDebugOutput(context);
            testFileFallback(context);
            testMacroBehavior(context);
            testStatsResetAndLifetimeDrops(context);
            testQueuePressureAndConcurrency(context, options);
            testFatalTerminateChild(context, options);
            runPerformanceMetrics(context, options);
            Logger::shutdown();
        }
        catch (const std::exception &exception)
        {
            Logger::shutdown();
            context.fail("uncaught exception", exception.what());
        }
        catch (...)
        {
            Logger::shutdown();
            context.fail("uncaught exception", "unknown exception");
        }

        const auto suiteEnd = Clock::now();
        const double suiteMilliseconds = std::chrono::duration<double, std::milli>(suiteEnd - suiteStart).count();
        printEndSummary(context, suiteMilliseconds);
        std::cout << std::format("[RESULT] passed={} failed={}\n", context.passed, context.failed);
        return context.failed == 0 ? 0 : 1;
    }
}
