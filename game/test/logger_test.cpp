/// @file logger_test.cpp
/// @brief Executable self-tests for the GameWIP Logger library.

#include "test/logger_test.h"

#include "logger/logger.h"
#include "logger/logger_macros.h"

#ifndef GAMEWIP_LOGGER_TEST_HOOKS
#define GAMEWIP_LOGGER_TEST_HOOKS 0
#endif

#if GAMEWIP_LOGGER_TEST_HOOKS
#include "logger/internal/logger_test_hooks.h"
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

namespace
{
    using Logger = GameWIP::Logger;
    using LoggerTestOptions = GameWIP::Test::LoggerTestOptions;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::string_view testSource = "LoggerTest";
    constexpr std::string_view shortMessage = "logger test message";
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "GAMEWIP_LOGGER_TEST_CHILD_LOG_DIR";
    constexpr std::string_view fatalTerminateChildMessage = "child fatal terminate";

    enum class TestSource : Logger::Types::SourceId
    {
        Core = 1,
        Render = 2,
        Audio = 3,
        Unknown = 99
    };

    struct MemoryPeak
    {
        Logger::Types::MemoryStats memory;
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
        std::size_t queueDropped = 0;
        std::size_t diagnosticFailures = 0;
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
        std::ofstream reportFile;

        void emit(std::string_view line)
        {
            std::cout << line;
            if (reportFile.is_open())
            {
                reportFile << line;
                reportFile.flush();
            }
        }

        void pass(std::string_view name)
        {
            ++passed;
            emit(std::format("[PASS] {}\n", name));
        }

        void fail(std::string_view name, std::string_view details)
        {
            ++failed;
            emit(std::format("[FAIL] {}: {}\n", name, details));
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

    void openReportFile(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.writeReport || options.reportPath.empty())
        {
            return;
        }

        const std::filesystem::path parentPath = options.reportPath.parent_path();
        if (!parentPath.empty())
        {
            std::filesystem::create_directories(parentPath);
        }

        const std::ios::openmode mode = options.appendReport
                                            ? (std::ios::out | std::ios::app)
                                            : (std::ios::out | std::ios::trunc);
        context.reportFile.open(options.reportPath, mode);

        if (context.reportFile.is_open())
        {
            context.emit(std::format("[INFO] Logger test report: {}\n", options.reportPath.string()));
        }
        else
        {
            std::cout << std::format("[WARN] Logger test report could not be opened: {}\n", options.reportPath.string());
        }
    }

    struct Timing
    {
        double milliseconds = 0.0;
    };

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

    template <typename Value>
    std::string printable(Value value)
    {
        if constexpr (std::is_same_v<Value, Logger::Types::Result>)
        {
            return std::string(toString(value));
        }
        else if constexpr (std::is_same_v<Value, Logger::Types::Output>)
        {
            return std::string(toString(value));
        }
        else if constexpr (std::is_same_v<Value, Logger::Types::Level>)
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

    std::string formatProcessBytes(const Logger::Types::MemoryStats &memory, std::size_t bytes)
    {
        return memory.processMemoryAvailable ? formatBytes(bytes) : "n/a";
    }

    Logger::Types::MemoryStats recordMemorySnapshot(TestContext &context, std::string_view label)
    {
        const Logger::Types::MemoryStats memory = Logger::getMemoryStats();
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

    std::size_t totalQueueDrops(const Logger::Types::Stats &stats)
    {
        return stats.queueDropsHard + stats.queueDropsSoft;
    }

    std::size_t totalDiagnosticFailures(const Logger::Types::Stats &stats)
    {
        return stats.allocationFailures + stats.formatFailures + stats.fileWriteFailures;
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

    void setEnvironmentVariableValue(std::string_view name, std::string_view value)
    {
        const std::string nameText(name);
        const std::string valueText(value);
#if defined(_WIN32)
        _putenv_s(nameText.c_str(), valueText.c_str());
        SetEnvironmentVariableA(nameText.c_str(), valueText.c_str());
#else
        setenv(nameText.c_str(), valueText.c_str(), 1);
#endif
    }

    void clearEnvironmentVariableValue(std::string_view name)
    {
        const std::string nameText(name);
#if defined(_WIN32)
        _putenv_s(nameText.c_str(), "");
        SetEnvironmentVariableA(nameText.c_str(), nullptr);
#else
        unsetenv(nameText.c_str());
#endif
    }

    struct ScopedEnvironmentVariable
    {
        std::string name;
        std::optional<std::string> previousValue;

        ScopedEnvironmentVariable(std::string_view variableName, std::string_view value)
            : name(variableName)
        {
            if (const char *previous = std::getenv(name.c_str()))
            {
                previousValue = previous;
            }
            setEnvironmentVariableValue(name, value);
        }

        ~ScopedEnvironmentVariable()
        {
            if (previousValue)
            {
                setEnvironmentVariableValue(name, *previousValue);
            }
            else
            {
                clearEnvironmentVariableValue(name);
            }
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
        ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;
    };

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

    std::string readDirectoryFiles(const std::filesystem::path &directory)
    {
        std::string contents;
        for (const std::filesystem::path &path : filesIn(directory))
        {
            contents += readWholeFile(path);
        }
        return contents;
    }

    struct OwnedLoggerConfig : Logger::Types::Config
    {
        std::string ownedLogDirectory;

        const Logger::Types::Config &ready()
        {
            logDirectory = ownedLogDirectory;
            return *this;
        }
    };

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

    OwnedLoggerConfig makeFileConfig(TestContext &context, std::string_view name, Logger::Types::Level minLevel = Logger::Types::Level::Trace)
    {
        return makeConfig(Logger::Types::Output::File, minLevel, testDirectory(context, name));
    }

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

    std::string makeLazyArgument(std::atomic<int> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
        return "lazy argument evaluated";
    }

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

        const Logger::Types::Result defaultResult = Logger::initDefault();
        const bool defaultStarted =
            defaultResult == Logger::Types::Result::Success ||
            defaultResult == Logger::Types::Result::FileOpenFailed ||
            defaultResult == Logger::Types::Result::FileSetupFailed;
        context.expectTrue("initDefault starts or falls back", defaultStarted);
        context.expectTrue("initDefault leaves logger running", Logger::isRunning());
        if (defaultResult == Logger::Types::Result::Success)
        {
            expectEq(context, "initDefault output", Logger::getOutput(), Logger::Types::Output::Both);
            context.expectFalse("initDefault path available", Logger::getLogFilePath().empty());
        }
        else
        {
            expectEq(context, "initDefault file failure falls back to console", Logger::getOutput(), Logger::Types::Output::Console);
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

    void testLevelAndSourceFilters(TestContext &context)
    {
        ZoneScopedN("Logger level and source filter tests");
        ScopedLoggerShutdown shutdown;

        std::array sources{
            Logger::defineSource(TestSource::Core, "Core"),
            Logger::defineSource(TestSource::Render, "Render"),
            Logger::defineSource(TestSource::Audio, "Audio")};
        std::array sourceFilters{
            Logger::Types::SourceFilter{static_cast<Logger::Types::SourceId>(TestSource::Render), false}};
        std::array levelFilters{
            Logger::Types::LevelFilter{Logger::Types::Level::Debug, false}};

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
        expectEq(context, "unknown source filter rejected", Logger::setSourceFilter(TestSource::Unknown, false), Logger::Types::Result::InvalidSourceFilter);
        Logger::clearSourceFilters();
        context.expectTrue("render source allowed after clear", Logger::shouldLog(Logger::Types::Level::Info, TestSource::Render));

        expectEq(context, "set level filter succeeds", Logger::setLevelFilter(Logger::Types::Level::Info, false), Logger::Types::Result::Success);
        context.expectFalse("info level filtered", Logger::shouldLog(Logger::Types::Level::Info));
        Logger::info(TestSource::Core, "filtered info");
        expectEq(context, "clear level filter succeeds", Logger::clearLevelFilter(Logger::Types::Level::Info), Logger::Types::Result::Success);
        Logger::clearLevelFilters();
        context.expectTrue("debug level allowed after clear", Logger::shouldLog(Logger::Types::Level::Debug));
        expectEq(context, "invalid runtime level filter rejected", Logger::setLevelFilter(static_cast<Logger::Types::Level>(99), true), Logger::Types::Result::InvalidLevelFilter);
    }

    void testSourceValidation(TestContext &context)
    {
        ZoneScopedN("Logger source validation tests");
        ScopedLoggerShutdown shutdown;

        std::array emptyNameSources{Logger::Types::SourceDefinition{1, {}}};
        Logger::Types::Config emptyNameConfig = makeConsoleConfig();
        emptyNameConfig.sources = emptyNameSources;
        expectEq(context, "empty source name rejected", Logger::init(emptyNameConfig), Logger::Types::Result::InvalidSourceDefinition);
        Logger::shutdown();

        std::array duplicateSources{
            Logger::Types::SourceDefinition{1, "Core"},
            Logger::Types::SourceDefinition{1, "CoreDuplicate"}};
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

        std::array duplicateFilters{
            Logger::Types::SourceFilter{1, false},
            Logger::Types::SourceFilter{1, true}};
        Logger::Types::Config duplicateFilterConfig = makeConsoleConfig();
        duplicateFilterConfig.sources = validSources;
        duplicateFilterConfig.sourceFilters = duplicateFilters;
        expectEq(context, "duplicate source filter rejected at init", Logger::init(duplicateFilterConfig), Logger::Types::Result::InvalidSourceFilter);
    }

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
        context.expectTrue("strict format flush", Logger::flush(2s));
        Logger::Types::Stats strictStats = Logger::getStats();
        expectEq(context, "strict format queued", strictStats.queued, std::size_t{3});
        expectEq(context, "strict runtime format failure counted", strictStats.formatFailures, std::size_t{1});
        expectEq(context, "strict truncation counted", strictStats.truncated, std::size_t{1});
        const std::string strictPath = Logger::getLogFilePath();
        Logger::shutdown();

        const std::string strictContents = readWholeFile(strictPath);
        context.expectTrue("strict compile format content", strictContents.find("value 12 ok") != std::string::npos);
        context.expectTrue("strict runtime format content", strictContents.find("runtime 13 ok") != std::string::npos);
        context.expectTrue("strict truncation suffix content", strictContents.find("[truncated]") != std::string::npos);

        OwnedLoggerConfig fastConfig = makeFileConfig(context, "format-fast", Logger::Types::Level::Trace);
        fastConfig.maxMessageLength = 48;
        fastConfig.formatPolicy = Logger::Types::FormatPolicy::FastNormal;
        expectInitSuccess(context, "fast format init", Logger::init(fastConfig.ready()));
        Logger::info(testSource, "fast {}", std::string(256, 'y'));
        context.expectTrue("fast format flush", Logger::flush(2s));
        const Logger::Types::Stats fastStats = Logger::getStats();
        expectEq(context, "fast truncation counted", fastStats.truncated, std::size_t{1});
    }

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
        context.expectTrue("source runtime report", Logger::report(Logger::Types::Level::Fatal, TestSource::Core, Logger::flushTimeout(2s), Logger::runtimeFormat("runtime fatal {}"), 22));
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
        context.expectTrue("report bypassed filters reached file", contents.find("report bypasses disabled core source and error level") != std::string::npos);
        context.expectTrue("report unknown source reached file", contents.find("report unknown source path") != std::string::npos);
        context.expectTrue("bad report format not emitted", contents.find("{}") == std::string::npos);
    }


    void testLoggerTestHooks(TestContext &context)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        ZoneScopedN("Logger test hook tests");
        GameWIP::LoggerDetail::TestHooks::reset();

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-file-open", Logger::Types::Level::Trace);
            GameWIP::LoggerDetail::TestHooks::forceNextFileOpenFailure();
            const Logger::Types::Result result = Logger::init(config.ready());
            expectEq(context, "hook forced file open retries successfully", result, Logger::Types::Result::Success);
            context.expectTrue("hook file open retry leaves logger running", Logger::isRunning());
            Logger::info(testSource, "file open retry still writes");
            context.expectTrue("hook file open retry flush", Logger::flush(2s));
            const std::string logFile = Logger::getLogFilePath();
            context.expectTrue("hook file open retry used next candidate", logFile.ends_with("_1.log"));
            Logger::shutdown();
            context.expectTrue("hook file open retry wrote content", readWholeFile(logFile).find("file open retry still writes") != std::string::npos);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-allocation", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook allocation init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::LoggerDetail::TestHooks::forceNextQueueAllocationFailure();
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
            GameWIP::LoggerDetail::TestHooks::forceNextFileFlushFailure();
            context.expectFalse("hook forced file flush failure", Logger::flush(2s));
            const Logger::Types::Stats stats = Logger::getStats();
            context.expectTrue("hook file flush failure counted", stats.fileWriteFailures > 0);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-file-write", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook file write init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::LoggerDetail::TestHooks::forceNextFileWriteFailure();
            Logger::report(Logger::Types::Level::Error, testSource, "forced file write failure report");
            const Logger::Types::Stats stats = Logger::getStats();
            context.expectTrue("hook file write failure counted", stats.fileWriteFailures > 0);
        }

        {
            ScopedLoggerShutdown shutdown;
            Logger::Types::Config config = makeConsoleConfig(Logger::Types::Level::Trace);
            config.enableFatalPopup = true;
            expectInitSuccess(context, "hook fatal popup init", Logger::init(config));
            GameWIP::LoggerDetail::TestHooks::forceNextFatalPopupFailure();
            Logger::report(Logger::Types::Level::Fatal, testSource, Logger::Types::ReportPopup::Fatal, "forced fatal popup failure");
            const Logger::Types::PlatformError error = Logger::getLastPlatformError();
            context.expectTrue("hook fatal popup failure source", error.source == Logger::Types::PlatformErrorSource::FatalPopup);
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-flush-timeout", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook timed flush init", Logger::init(config.ready()));
            GameWIP::LoggerDetail::TestHooks::forceNextTimedFlushTimeout();
            context.expectFalse("hook timed flush timeout", Logger::flush(2s));
        }

        {
            ScopedLoggerShutdown shutdown;
            OwnedLoggerConfig config = makeFileConfig(context, "hook-report-timeout", Logger::Types::Level::Trace);
            expectInitSuccess(context, "hook report timeout init", Logger::init(config.ready()));
            Logger::resetStats();
            GameWIP::LoggerDetail::TestHooks::forceNextTimedFlushTimeout();
            const bool flushed = Logger::reportError(testSource, Logger::flushTimeout(2s), "report timeout still writes first");
            context.expectFalse("hook report timeout returns bounded drain failure", flushed);
            const Logger::Types::Stats stats = Logger::getStats();
            expectEq(context, "hook report timeout not queued", stats.queued, std::size_t{0});
            expectEq(context, "hook report timeout written synchronously", stats.written, std::size_t{1});
            const std::string logFile = Logger::getLogFilePath();
            Logger::shutdown();
            context.expectTrue("hook report timeout line reached file", readWholeFile(logFile).find("report timeout still writes first") != std::string::npos);
        }

        GameWIP::LoggerDetail::TestHooks::reset();
#else
        context.pass("logger test hooks skipped because GAMEWIP_LOGGER_TEST_HOOKS=0");
#endif
    }

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
        while (attempts.load(std::memory_order_acquire) < 1024 &&
               Clock::now() - waitStart < 500ms)
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

        context.expectEq(
            "flush active producers attempts completed",
            flushSuccesses + flushTimeouts,
            static_cast<std::size_t>(flushCount));
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
        context.emit(std::format(
            "[STRESS] flushWhileProducersActive threads={} flushAttempts={} flushSuccesses={} flushTimeouts={} producerAttempts={} written={} queueDropped={} peakQueue={}\n",
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
        std::this_thread::sleep_for(5ms);
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

    int runChildProcess(std::string_view executablePath, std::string_view argument)
    {
#if defined(_WIN32)
        std::string commandLine = quoteCommandArg(executablePath) + " " + std::string(argument);

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE nullOutput = CreateFileA(
            "NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &securityAttributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (nullOutput == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startupInfo.hStdOutput = nullOutput;
        startupInfo.hStdError = nullOutput;

        PROCESS_INFORMATION processInfo{};
        const BOOL created = CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo);
        CloseHandle(nullOutput);

        if (!created)
        {
            return 0;
        }

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return static_cast<int>(exitCode);
#else
        std::string command = quoteCommandArg(executablePath) + " " + std::string(argument);
        command += " > /dev/null 2> /dev/null";
        return std::system(command.c_str());
#endif
    }

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

    void testFatalTerminateChild(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableChildCrashTests)
        {
            context.pass("fatalTerminate child test disabled by LoggerTestOptions");
            return;
        }

        ZoneScopedN("Logger fatalTerminate child test");
        const std::filesystem::path childLogDirectory = context.logRoot / "fatal_terminate_child";
        std::filesystem::create_directories(childLogDirectory);
        const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));

        std::cout.flush();
        std::cerr.flush();
        const int result = runChildProcess(context.executablePath, "--logger-test-child=fatal-terminate");
        context.expectTrue("fatalTerminate child exits abnormally", result != 0, "child process returned zero");
        const std::string childLogContents = readDirectoryFiles(childLogDirectory);
        context.expectTrue("fatalTerminate child logs fatal through report", childLogContents.find("[FATAL][LoggerTest]: child fatal terminate") != std::string::npos, "fatalTerminate child log missing");
        std::cout.flush();
    }

    void printMetric(TestContext &context, std::string_view name, std::size_t iterations, double milliseconds, const Logger::Types::Stats &stats)
    {
        const double nanosecondsPerCall = iterations == 0 ? 0.0 : (milliseconds * 1'000'000.0) / static_cast<double>(iterations);
        const std::size_t queueDrops = totalQueueDrops(stats);
        const std::size_t diagnosticFailures = totalDiagnosticFailures(stats);
        const Logger::Types::MemoryStats memory = recordMemorySnapshot(context, name);
        ++context.performanceTotals.scenarioCount;
        context.performanceTotals.measuredMessages += iterations;
        context.performanceTotals.producerMilliseconds += milliseconds;
        context.performanceTotals.queued += stats.queued;
        context.performanceTotals.written += stats.written;
        context.performanceTotals.queueDropped += queueDrops;
        context.performanceTotals.diagnosticFailures += diagnosticFailures;
        context.performanceTotals.truncated += stats.truncated;
        context.performanceTotals.peakQueueDepth = std::max(context.performanceTotals.peakQueueDepth, stats.peakQueueDepth);

        context.emit(std::format(
            "[METRIC] {} iterations={} ms={:.3f} nsPerCall={:.2f} queued={} written={} queueDropped={} diagnostics={} truncated={} peak={} loggerRetained={} processPrivate={} processWorkingSet={}\n",
            name,
            iterations,
            milliseconds,
            nanosecondsPerCall,
            stats.queued,
            stats.written,
            queueDrops,
            diagnosticFailures,
            stats.truncated,
            stats.peakQueueDepth,
            formatBytes(memory.loggerRetainedBytes),
            formatProcessBytes(memory, memory.processPrivateBytes),
            formatProcessBytes(memory, memory.processWorkingSetBytes)));
        TracyPlot("Logger test metric ms", milliseconds);
        TracyPlot("Logger test metric ns/call", nanosecondsPerCall);
        TracyPlot("Logger test memory retained bytes", static_cast<double>(memory.loggerRetainedBytes));
        if (memory.processMemoryAvailable)
        {
            TracyPlot("Logger test process private bytes", static_cast<double>(memory.processPrivateBytes));
            TracyPlot("Logger test process working set bytes", static_cast<double>(memory.processWorkingSetBytes));
        }
    }

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
            Logger::Types::Config config = makeConsoleConfig(Logger::Types::Level::Trace);
            config.output = Logger::Types::Output::None;
            expectEq(context, "perf disabled init", Logger::init(config), Logger::Types::Result::Success);
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
            OwnedLoggerConfig config = makeFileConfig(context, "perf-filtered", Logger::Types::Level::Fatal);
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
            OwnedLoggerConfig config = makeFileConfig(context, "perf-enabled", Logger::Types::Level::Info);
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
        const Logger::Types::MemoryStats finalMemory = recordMemorySnapshot(context, "suite-end");
        const PerformanceTotals &totals = context.performanceTotals;
        const double totalNsPerMessage = totals.measuredMessages == 0
                                             ? 0.0
                                             : (totals.producerMilliseconds * 1'000'000.0) / static_cast<double>(totals.measuredMessages);

        context.emit(std::format(
            "[SUMMARY] suiteTimeMs={:.3f} tests={} passed={} failed={}\n",
            suiteMilliseconds,
            context.passed + context.failed,
            context.passed,
            context.failed));

        context.emit(std::format(
            "[SUMMARY] perfTotals scenarios={} measuredMessages={} producerMs={:.3f} nsPerMessage={:.2f} queued={} written={} queueDropped={} diagnostics={} truncated={} peakQueue={}\n",
            totals.scenarioCount,
            totals.measuredMessages,
            totals.producerMilliseconds,
            totalNsPerMessage,
            totals.queued,
            totals.written,
            totals.queueDropped,
            totals.diagnosticFailures,
            totals.truncated,
            totals.peakQueueDepth));

        if (context.loggerMemoryPeak.available)
        {
            const Logger::Types::MemoryStats &peak = context.loggerMemoryPeak.memory;
            context.emit(std::format(
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
            context.emit(std::format(
                "[SUMMARY] processMemoryPeak label={} private={} workingSet={}\n",
                context.processMemoryPeak.label,
                formatBytes(peak.processPrivateBytes),
                formatBytes(peak.processWorkingSetBytes)));
        }

        context.emit(std::format(
            "[SUMMARY] finalMemory loggerRetained={} processPrivate={} processWorkingSet={}\n",
            formatBytes(finalMemory.loggerRetainedBytes),
            formatProcessBytes(finalMemory, finalMemory.processPrivateBytes),
            formatProcessBytes(finalMemory, finalMemory.processWorkingSetBytes)));
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

        openReportFile(context, options);
        context.emit(std::format("[INFO] Logger test log root: {}\n", pathText(context.logRoot)));
        context.emit(std::format(
            "[INFO] Logger test options: stress={} fatalChild={} performance={} manualUi={} loggerPopup={} perfIterations={} stressThreads={} stressIterations={} report={}\n",
            options.enableStressTests,
            options.enableChildCrashTests,
            options.enablePerformanceMetrics,
            options.enableManualUiTests,
            options.enableLoggerPopupTest,
            options.performanceIterations,
            options.stressThreadCount,
            options.stressIterationsPerThread,
            options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

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
            testReportFailureAndUnknownSourcePaths(context);
            testLoggerTestHooks(context);
            testFileFallback(context);
            testMacroBehavior(context);
            testFilteredLogsDoNotCountAsDrops(context);
            testStatsResetKeepsLifetimeQueueDrops(context);
            testQueuePressureAndConcurrency(context, options);
            testReportBypassesFullQueue(context, options);
            testReportWhileProducersActive(context, options);
            testFlushWhileProducersActive(context, options);
            testShutdownWhileProducersActive(context, options);
            testRepeatedInitShutdownStress(context, options);
            testFatalTerminateChild(context, options);
            testManualLoggerFatalPopup(context, options);
            if (options.enableManualUiTests)
            {
                context.pass("manual logger UI tests enabled; add explicit manual scenarios before running unattended");
            }
            else
            {
                context.pass("manual logger UI tests skipped by LoggerTestOptions");
            }
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
        context.emit(std::format("[RESULT] logger passed={} failed={}\n", context.passed, context.failed));
        return context.failed == 0 ? 0 : 1;
    }
}
