/// @file logger_test.cpp
/// @brief Executable self-tests for the Logger library.

#include "validation/tests/logger/logger_test.h"

#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "test_support/test_support.h"
#include "unicode/unicode.h"

#ifndef INTERNAL_LOGGER_TEST_HOOKS
#define INTERNAL_LOGGER_TEST_HOOKS 0
#endif

#if INTERNAL_LOGGER_TEST_HOOKS
#include "logger/internal/logger_test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
    namespace Logger = GameWIP::Logger;
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;
    using LoggerTestOptions = GameWIP::Test::LoggerTestOptions;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::string_view testSource = "LoggerTest";
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "INTERNAL_LOGGER_TEST_CHILD_LOG_DIR";
    constexpr std::string_view fatalTerminateChildArgument = "--logger-test-child=fatal-terminate";
    constexpr std::string_view fatalTerminateChildMessage = "child fatal terminate";

    enum class TestSource : Logger::Types::SourceId
    {
        Core = 1,
        Render = 2,
        Unknown = 99
    };

    struct TestContext
    {
        explicit TestContext(TestSupport::Context &testContext) noexcept
            : testContext(testContext)
        {
        }

        TestSupport::Context &testContext;
        std::filesystem::path logRoot;
        std::string executablePath;

        void pass(std::string_view name)
        {
            testContext.pass(name);
        }
        void fail(std::string_view name, std::string_view details)
        {
            testContext.fail(name, details);
        }

        void expectTrue(std::string_view name, bool value, std::string_view details = "expected true")
        {
            if (value)
            {
                testContext.pass(name);
            }
            else
            {
                testContext.fail(name, details);
            }
        }

        void expectFalse(std::string_view name, bool value, std::string_view details = "expected false")
        {
            expectTrue(name, !value, details);
        }

        template <typename Left, typename Right> void expectEq(std::string_view name, const Left &actual, const Right &expected)
        {
            static_cast<void>(testContext.expectEq(name, expected, actual));
        }

        void expectContains(std::string_view name, std::string_view text, std::string_view needle)
        {
            static_cast<void>(testContext.expectContains(name, text, needle));
        }
    };

    template <typename Function> void runCase(TestContext &context, std::string_view name, Function &&function)
    {
        TestSupport::Section section(context.testContext, name);
        try
        {
            std::forward<Function>(function)();
        }
        catch (const std::exception &exception)
        {
            static_cast<void>(Logger::shutdown());
            context.fail(name, exception.what());
        }
        catch (...)
        {
            static_cast<void>(Logger::shutdown());
            context.fail(name, "unknown exception");
        }
    }

    struct ScopedLoggerShutdown
    {
        ~ScopedLoggerShutdown()
        {
            static_cast<void>(Logger::shutdown());
        }
    };

    [[nodiscard]] bool flushCompleted(const Logger::Types::FlushResult &result) noexcept
    {
        return result.status.ok() && result.outcome == Logger::Types::FlushOutcome::Completed;
    }

    [[nodiscard]] bool reportDelivered(const Logger::Types::ReportResult &result) noexcept
    {
        return result.status.ok() && result.outcome == Logger::Types::ReportOutcome::Completed &&
               result.delivery != Logger::Types::ReportDelivery::None;
    }

    [[nodiscard]] std::string pathText(const std::filesystem::path &path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    }

    [[nodiscard]] std::filesystem::path pathFromText(std::string_view text)
    {
        const auto *begin = reinterpret_cast<const char8_t *>(text.data());
        return std::filesystem::path(std::u8string(begin, begin + text.size()));
    }

    [[nodiscard]] std::string readWholeFile(TestContext &context, const std::filesystem::path &path)
    {
        TestSupport::Types::TextResult result = TestSupport::readTextFile(path);
        if (!result.status.ok())
        {
            context.fail("read Logger log fixture", TestSupport::formatInfrastructureStatus(result.status));
            return {};
        }
        return std::move(result.text);
    }

    [[nodiscard]] std::filesystem::path testDirectory(TestContext &context, std::string_view name)
    {
        std::filesystem::path directory = context.logRoot / std::string(name);
        const auto status = TestSupport::createDirectories(directory);
        if (!status.ok())
        {
            context.fail("create Logger test directory", TestSupport::formatInfrastructureStatus(status));
        }
        return directory;
    }

    struct OwnedLoggerConfig : Logger::Types::Config
    {
        std::string ownedDirectory;

        const Logger::Types::Config &ready()
        {
            logDirectory = ownedDirectory;
            return *this;
        }
    };

    [[nodiscard]] Logger::Types::Config makeConsoleConfig(Logger::Types::Level minLevel = Logger::Types::Level::Trace)
    {
        Logger::Types::Config config;
        config.output = Logger::Types::OutputMode::Console;
        config.minLevel = minLevel;
        config.maxQueueSize = 256;
        config.maxMessageLength = 512;
        config.inlineMessageCapacity = 128;
        config.workerBatchSize = 64;
        config.enableConsoleColor = false;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        config.releaseMessageMemoryAfterWrite = true;
        config.releaseStorageOnShutdown = true;
        return config;
    }

    [[nodiscard]] OwnedLoggerConfig makeFileConfig(
        TestContext &context,
        std::string_view name,
        Logger::Types::Level minLevel = Logger::Types::Level::Trace)
    {
        OwnedLoggerConfig config;
        static_cast<Logger::Types::Config &>(config) = makeConsoleConfig(minLevel);
        config.output = Logger::Types::OutputMode::File;
        config.fallbackToConsoleOnFileFailure = false;
        config.ownedDirectory = pathText(testDirectory(context, name));
        config.logDirectory = config.ownedDirectory;
        return config;
    }

    void expectStarted(TestContext &context, std::string_view name, const Logger::Types::InitResult &result)
    {
        context.expectTrue(
            name,
            result.status.ok() && result.outcome == Logger::Types::InitOutcome::Started,
            std::format(
                "status={} native={} outcome={} requested={} effective={} outputStatus={} outputNative={}",
                static_cast<int>(result.status.code),
                result.status.nativeCode,
                static_cast<int>(result.outcome),
                static_cast<int>(result.requestedOutput),
                static_cast<int>(result.effectiveOutput),
                static_cast<int>(result.outputSetupStatus.code),
                result.outputSetupStatus.nativeCode));
    }

    void testInitResultModel(TestContext &context)
    {
        ScopedLoggerShutdown shutdown;

        Logger::Types::Config disabled = makeConsoleConfig();
        disabled.output = Logger::Types::OutputMode::None;
        const Logger::Types::InitResult disabledResult = Logger::init(disabled);
        context.expectTrue("disabled init status success", disabledResult.status.ok());
        context.expectEq("disabled init outcome", disabledResult.outcome, Logger::Types::InitOutcome::Disabled);
        context.expectEq("disabled init effective output", disabledResult.effectiveOutput, Logger::Types::OutputMode::None);
        context.expectFalse("disabled init not running", Logger::isRunning());
        static_cast<void>(Logger::shutdown());

        Logger::Types::Config adjusted = makeConsoleConfig();
        adjusted.maxQueueSize = 0;
        adjusted.maxMessageLength = 0;
        adjusted.inlineMessageCapacity = 4096;
        adjusted.workerBatchSize = 9999;
        const Logger::Types::InitResult adjustedResult = Logger::init(adjusted);
        expectStarted(context, "adjusted init succeeds", adjustedResult);
        context.expectTrue(
            "queue adjustment reported",
            Logger::Types::hasAdjustment(adjustedResult.adjustments, Logger::Types::InitAdjustment::QueueLimitsAdjusted));
        context.expectTrue(
            "message adjustment reported",
            Logger::Types::hasAdjustment(adjustedResult.adjustments, Logger::Types::InitAdjustment::MessageLengthAdjusted));
        context.expectTrue(
            "inline adjustment reported",
            Logger::Types::hasAdjustment(adjustedResult.adjustments, Logger::Types::InitAdjustment::InlineCapacityAdjusted));
        context.expectTrue(
            "worker batch adjustment reported",
            Logger::Types::hasAdjustment(adjustedResult.adjustments, Logger::Types::InitAdjustment::WorkerBatchAdjusted));

        const Logger::Types::InitResult alreadyRunning = Logger::init(makeConsoleConfig());
        context.expectEq("second init is AlreadyOpen", alreadyRunning.status.code, IO::Types::ErrorCode::AlreadyOpen);
        context.expectEq("second init preserves started state", alreadyRunning.outcome, Logger::Types::InitOutcome::Started);
        context.expectTrue("second init leaves existing Logger running", Logger::isRunning());
        static_cast<void>(Logger::shutdown());

        Logger::Types::Config storageFallback = makeConsoleConfig();
        storageFallback.maxQueueSize = std::numeric_limits<std::size_t>::max();
        storageFallback.workerBatchSize = 9999;
        const Logger::Types::InitResult storageFallbackResult = Logger::init(storageFallback);
        expectStarted(context, "queue storage fallback succeeds", storageFallbackResult);
        context.expectTrue(
            "queue storage fallback reported",
            Logger::Types::hasAdjustment(storageFallbackResult.adjustments, Logger::Types::InitAdjustment::QueueStorageFallback));
        context.expectTrue(
            "fallback worker batch adjustment reported",
            Logger::Types::hasAdjustment(storageFallbackResult.adjustments, Logger::Types::InitAdjustment::WorkerBatchAdjusted));
        static_cast<void>(Logger::shutdown());

        Logger::Types::Config invalidOutput = makeConsoleConfig();
        invalidOutput.output = static_cast<Logger::Types::OutputMode>(99);
        context.expectEq("invalid output rejected", Logger::init(invalidOutput).status.code, IO::Types::ErrorCode::InvalidArgument);

        Logger::Types::Config invalidLevel = makeConsoleConfig();
        invalidLevel.minLevel = static_cast<Logger::Types::Level>(99);
        context.expectEq("invalid level rejected", Logger::init(invalidLevel).status.code, IO::Types::ErrorCode::InvalidArgument);

        Logger::Types::Config invalidPolicy = makeConsoleConfig();
        invalidPolicy.formatPolicy = static_cast<Logger::Types::FormatPolicy>(99);
        context.expectEq("invalid format policy rejected", Logger::init(invalidPolicy).status.code, IO::Types::ErrorCode::InvalidArgument);

        const std::string invalidUtf8{"\xC3\x28", 2};
        Logger::Types::Config invalidDirectory = makeConsoleConfig();
        invalidDirectory.logDirectory = invalidUtf8;
        context.expectEq("invalid UTF-8 directory rejected", Logger::init(invalidDirectory).status.code, IO::Types::ErrorCode::EncodingFailed);

        std::array invalidUtf8Sources{Logger::Types::SourceDefinition{1, std::string_view{"\xC3\x28", 2}}};
        Logger::Types::Config invalidSourceText = makeConsoleConfig();
        invalidSourceText.sources = invalidUtf8Sources;
        context.expectEq("invalid UTF-8 source rejected", Logger::init(invalidSourceText).status.code, IO::Types::ErrorCode::EncodingFailed);
    }

    void testFileFallbackAndSetupStatus(TestContext &context)
    {
        ScopedLoggerShutdown shutdown;
        const std::filesystem::path blockingPath = context.logRoot / "not-a-directory";
        {
            std::ofstream file(blockingPath);
            file << "blocks directory creation";
        }
        const std::string blockingText = pathText(blockingPath);

        Logger::Types::Config noFallback = makeConsoleConfig();
        noFallback.output = Logger::Types::OutputMode::File;
        noFallback.logDirectory = blockingText;
        noFallback.fallbackToConsoleOnFileFailure = false;
        const Logger::Types::InitResult failed = Logger::init(noFallback);
        context.expectFalse("file setup without fallback fails overall", failed.status.ok());
        context.expectFalse("file setup failure retained", failed.outputSetupStatus.ok());
        context.expectEq("file setup failure disabled", failed.outcome, Logger::Types::InitOutcome::Disabled);
        context.expectEq("file setup failure output none", failed.effectiveOutput, Logger::Types::OutputMode::None);
        static_cast<void>(Logger::shutdown());

        Logger::Types::Config fallback = noFallback;
        fallback.fallbackToConsoleOnFileFailure = true;
        const Logger::Types::InitResult recovered = Logger::init(fallback);
        expectStarted(context, "file setup fallback starts", recovered);
        context.expectFalse("fallback retains requested file failure", recovered.outputSetupStatus.ok());
        context.expectEq("fallback effective output console", recovered.effectiveOutput, Logger::Types::OutputMode::Console);
        context.expectEq("fallback health degraded", Logger::getHealth().state, Logger::Types::HealthState::Degraded);
    }

    void testFileLoggingAndUtf8Truncation(TestContext &context)
    {
        ScopedLoggerShutdown shutdown;
        OwnedLoggerConfig config = makeFileConfig(context, "utf8-truncation");
        config.maxMessageLength = 24;
        config.inlineMessageCapacity = 16;
        expectStarted(context, "UTF-8 file init", Logger::init(config.ready()));

        const std::string message = "prefix \xF0\x9F\x98\x80 \xE2\x98\x85 payload that truncates";
        Logger::info(testSource, message);
        Logger::info(testSource, "formatted {}", message);
        const Logger::Types::ReportResult report = Logger::reportError(testSource, "formatted {}", message);
        context.expectTrue("UTF-8 formatted report delivered", reportDelivered(report));
        context.expectTrue("UTF-8 file flush", flushCompleted(Logger::flush(2s)));
        const std::string logFile = Logger::getLogFilePath();
        static_cast<void>(Logger::shutdown());

        const std::string contents = readWholeFile(context, pathFromText(logFile));
        context.expectContains("truncation suffix written", contents, "[truncated]");
        context.expectTrue("UTF-8 prefix retained", contents.find("prefix ") != std::string::npos);
        context.expectContains("strict formatted truncation suffix written", contents, "formatted... [truncated]");
        context.expectEq(
            "Logger-owned truncation preserves valid UTF-8",
            GameWIP::Unicode::Utf8::validate(contents).outcome,
            GameWIP::Unicode::Types::ValidationOutcome::Valid);
    }

    void testFilterStatusesAndConcurrency(TestContext &context, const LoggerTestOptions &options)
    {
        ScopedLoggerShutdown shutdown;
        std::array sources{Logger::defineSource(TestSource::Core, "Core"), Logger::defineSource(TestSource::Render, "Render")};
        OwnedLoggerConfig config = makeFileConfig(context, "filters");
        config.sources = sources;
        expectStarted(context, "filter init", Logger::init(config.ready()));

        context.expectTrue("set source filter status", Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Render), false).ok());
        context.expectFalse("render filtered", Logger::shouldLog(Logger::Types::Level::Info, TestSource::Render));
        context.expectEq(
            "unknown source filter NotFound",
            Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Unknown), false).code,
            IO::Types::ErrorCode::NotFound);
        context.expectEq(
            "invalid level filter InvalidArgument",
            Logger::setLevelFilter(static_cast<Logger::Types::Level>(99), true).code,
            IO::Types::ErrorCode::InvalidArgument);
        context.expectTrue("clear source filters status", Logger::clearSourceFilters().ok());
        context.expectTrue("set level filter status", Logger::setLevelFilter(Logger::Types::Level::Debug, false).ok());
        context.expectFalse("debug filtered", Logger::shouldLog(Logger::Types::Level::Debug));
        context.expectTrue("clear level filters status", Logger::clearLevelFilters().ok());

        if (!options.enableStressTests)
        {
            context.pass("filter concurrency skipped by LoggerTestOptions");
            return;
        }

        const std::size_t iterations = std::max<std::size_t>(500, options.stressIterationsPerThread);
        std::atomic<bool> start{false};
        std::atomic<bool> mutationsOk{true};
        std::thread producer(
            [&]
            {
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                for (std::size_t i = 0; i < iterations; ++i)
                    Logger::info(TestSource::Core, "producer {}", i);
            });
        std::thread filter(
            [&]
            {
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                for (std::size_t i = 0; i < iterations; ++i)
                {
                    if (!Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Core), (i & 1u) == 0).ok())
                        mutationsOk.store(false, std::memory_order_relaxed);
                }
            });
        start.store(true, std::memory_order_release);
        producer.join();
        filter.join();
        context.expectTrue("concurrent filter mutations succeed", mutationsOk.load(std::memory_order_relaxed));
        static_cast<void>(Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Core), true));
        context.expectTrue("concurrent filter final flush", flushCompleted(Logger::flush(5s)));
    }

    void testReportsAndHealth(TestContext &context)
    {
        ScopedLoggerShutdown shutdown;
        OwnedLoggerConfig config = makeFileConfig(context, "reports", Logger::Types::Level::Fatal);
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        expectStarted(context, "report init", Logger::init(config.ready()));

        context.expectFalse("normal Info remains filtered", Logger::shouldLog(Logger::Types::Level::Info));
        const Logger::Types::ReportResult plain = Logger::reportError(testSource, "plain emergency report");
        context.expectTrue("emergency report delivered", reportDelivered(plain));
        context.expectEq("emergency report complete delivery", plain.delivery, Logger::Types::ReportDelivery::Complete);
        const Logger::Types::ReportResult formatted = Logger::report(Logger::Types::Level::Warn, testSource, "value {}", 17);
        context.expectTrue("formatted emergency report delivered", reportDelivered(formatted));
        const Logger::Types::ReportResult enumFormatted = Logger::report(Logger::Types::Level::Warn, TestSource::Core, "enum value {}", 23);
        context.expectTrue("enum-source formatted emergency report delivered", reportDelivered(enumFormatted));
        const Logger::Types::ReportResult enumRuntime = Logger::reportError(TestSource::Core, 2s, Logger::runtimeFormat("enum runtime {}"), 29);
        context.expectTrue("enum-source runtime emergency report delivered", reportDelivered(enumRuntime));

        const Logger::Types::ReportResult invalidTimeout = Logger::reportError(testSource, -1ms, "invalid timeout");
        context.expectEq("negative report timeout invalid argument", invalidTimeout.status.code, IO::Types::ErrorCode::InvalidArgument);
        const std::string invalidUtf8{"\xC3\x28", 2};
        context.expectEq(
            "invalid report source UTF-8 rejected",
            Logger::reportError(invalidUtf8, "message").status.code,
            IO::Types::ErrorCode::EncodingFailed);
        context.expectEq(
            "invalid report message UTF-8 rejected",
            Logger::reportError(testSource, invalidUtf8).status.code,
            IO::Types::ErrorCode::EncodingFailed);

#if INTERNAL_LOGGER_TEST_HOOKS
        GameWIP::Logger::TestHooks::forceNextTimedFlushTimeout();
        const Logger::Types::ReportResult timedOut = Logger::reportError(testSource, 2s, "delivered before report timeout");
        context.expectTrue("report timeout is not IO failure", timedOut.status.ok());
        context.expectEq("report timeout has domain outcome", timedOut.outcome, Logger::Types::ReportOutcome::TimedOut);
        context.expectEq("report timeout preserves delivery", timedOut.delivery, Logger::Types::ReportDelivery::Complete);

        GameWIP::Logger::TestHooks::forceNextFileFlushFailure();
        const Logger::Types::ReportResult flushFailed = Logger::reportError(testSource, "delivered before flush failure");
        context.expectFalse("report surfaces post-delivery flush failure", flushFailed.status.ok());
        context.expectEq("report flush failure completes attempt", flushFailed.outcome, Logger::Types::ReportOutcome::Completed);
        context.expectEq("report flush failure preserves complete delivery", flushFailed.delivery, Logger::Types::ReportDelivery::Complete);

        static_cast<void>(Logger::shutdown());
        config.output = Logger::Types::OutputMode::Both;
        expectStarted(context, "report partial-delivery reinit", Logger::init(config.ready()));
        GameWIP::Logger::TestHooks::forceNextFileWriteFailure();
        const Logger::Types::ReportResult failed = Logger::reportError(testSource, "forced file write failure");
        context.expectFalse("report surfaces sink failure", failed.status.ok());
        context.expectEq("remaining console sink gives partial delivery", failed.delivery, Logger::Types::ReportDelivery::Partial);
        const Logger::Types::HealthSnapshot health = Logger::getHealth();
        context.expectEq("file failure degrades only failed sink", health.state, Logger::Types::HealthState::Degraded);
        context.expectEq("file failure health source", health.lastFailureSource, Logger::Types::FailureSource::File);
        context.expectEq("file failure preserves console output", health.effectiveOutput, Logger::Types::OutputMode::Console);
#endif
    }

    void testReportDoesNotDrainAsyncBacklog(TestContext &context)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        ScopedLoggerShutdown shutdown;
        OwnedLoggerConfig config = makeFileConfig(context, "report-no-drain");
        config.workerBatchSize = 1;
        expectStarted(context, "report no-drain init", Logger::init(config.ready()));

        GameWIP::Logger::TestHooks::armWorkerDeliveryPause();
        Logger::info(testSource, "older queued line deliberately held");
        GameWIP::Logger::TestHooks::waitForWorkerDeliveryPause();

        const auto start = Clock::now();
        const Logger::Types::ReportResult report = Logger::reportError(testSource, 250ms, "emergency line bypasses old queue");
        const auto elapsed = Clock::now() - start;
        context.expectTrue("timed report completes with older queue blocked", reportDelivered(report));
        context.expectTrue("timed report does not wait for older queue", elapsed < 1s);

        GameWIP::Logger::TestHooks::releaseWorkerDeliveryPause();
        context.expectTrue("report no-drain final flush", flushCompleted(Logger::flush(2s)));
#else
        context.pass("report no-drain hook test skipped because INTERNAL_LOGGER_TEST_HOOKS=0");
#endif
    }

    void testFlushContracts(TestContext &context)
    {
        ScopedLoggerShutdown shutdown;
        OwnedLoggerConfig config = makeFileConfig(context, "flush-contracts");
        expectStarted(context, "flush contract init", Logger::init(config.ready()));
        Logger::info(testSource, "flush me");

        context.expectTrue("indefinite flush completes", flushCompleted(Logger::flush()));
        const Logger::Types::FlushResult invalid = Logger::flush(-1ms);
        context.expectEq("negative flush timeout invalid", invalid.status.code, IO::Types::ErrorCode::InvalidArgument);

#if INTERNAL_LOGGER_TEST_HOOKS
        GameWIP::Logger::TestHooks::forceNextTimedFlushTimeout();
        const Logger::Types::FlushResult timedOut = Logger::flush(2s);
        context.expectTrue("timeout is not IO failure", timedOut.status.ok());
        context.expectEq("timeout has domain outcome", timedOut.outcome, Logger::Types::FlushOutcome::TimedOut);

        GameWIP::Logger::TestHooks::forceNextFileFlushFailure();
        const Logger::Types::FlushResult failed = Logger::flush(2s);
        context.expectFalse("flush sink failure is IO failure", failed.status.ok());
        context.expectEq("flush sink failure still completed attempt", failed.outcome, Logger::Types::FlushOutcome::Completed);
        context.expectEq("flush failure health source", Logger::getHealth().lastFailureSource, Logger::Types::FailureSource::File);
#endif
    }

    void testFatalPopupFailure(TestContext &context)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        ScopedLoggerShutdown shutdown;
        OwnedLoggerConfig config = makeFileConfig(context, "fatal-popup");
        config.enableFatalPopup = true;
        expectStarted(context, "fatal popup init", Logger::init(config.ready()));

        GameWIP::Logger::TestHooks::forceNextFatalPopupFailure();
        const Logger::Types::ReportResult result = Logger::reportFatal(testSource, "forced popup failure");
        context.expectFalse("fatal popup failure returned directly", result.status.ok());
        context.expectEq("fatal popup partial delivery", result.delivery, Logger::Types::ReportDelivery::Partial);
        context.expectEq("fatal popup health source", Logger::getHealth().lastFailureSource, Logger::Types::FailureSource::FatalPopup);
#else
        context.pass("fatal popup failure hook test skipped because INTERNAL_LOGGER_TEST_HOOKS=0");
#endif
    }

    void testShutdownAndHealthEpoch(TestContext &context)
    {
        ScopedLoggerShutdown shutdown;
        OwnedLoggerConfig config = makeFileConfig(context, "health-epoch");
        expectStarted(context, "health epoch init", Logger::init(config.ready()));

#if INTERNAL_LOGGER_TEST_HOOKS
        std::atomic<bool> observeHealth{true};
        std::atomic<bool> healthSnapshotsCoherent{true};
        std::thread healthObserver(
            [&]
            {
                while (observeHealth.load(std::memory_order_acquire))
                {
                    const Logger::Types::HealthSnapshot snapshot = Logger::getHealth();
                    const bool healthy = snapshot.state == Logger::Types::HealthState::Healthy &&
                                         snapshot.effectiveOutput == Logger::Types::OutputMode::File &&
                                         snapshot.lastFailureSource == Logger::Types::FailureSource::None &&
                                         snapshot.lastError == IO::Types::ErrorCode::Success && snapshot.failureCount == 0;
                    const bool failed = snapshot.state == Logger::Types::HealthState::Disabled &&
                                        snapshot.effectiveOutput == Logger::Types::OutputMode::None &&
                                        snapshot.lastFailureSource == Logger::Types::FailureSource::File &&
                                        snapshot.lastError != IO::Types::ErrorCode::Success && snapshot.failureCount > 0;
                    if (!healthy && !failed)
                        healthSnapshotsCoherent.store(false, std::memory_order_release);
                }
            });
        GameWIP::Logger::TestHooks::forceNextFileWriteFailure();
        Logger::info(testSource, "worker failure");
        static_cast<void>(Logger::flush(2s));
        observeHealth.store(false, std::memory_order_release);
        healthObserver.join();
        context.expectTrue("concurrent health snapshots remain coherent", healthSnapshotsCoherent.load(std::memory_order_acquire));
        const Logger::Types::HealthSnapshot failedHealth = Logger::getHealth();
        context.expectTrue("health records failure count", failedHealth.failureCount > 0);
        Logger::resetStats();
        const Logger::Types::HealthSnapshot healthAfterStatsReset = Logger::getHealth();
        context.expectEq("resetStats preserves health state", healthAfterStatsReset.state, failedHealth.state);
        context.expectEq("resetStats preserves health source", healthAfterStatsReset.lastFailureSource, failedHealth.lastFailureSource);
        context.expectEq("resetStats preserves health error", healthAfterStatsReset.lastError, failedHealth.lastError);
        context.expectEq("resetStats preserves health native code", healthAfterStatsReset.lastNativeCode, failedHealth.lastNativeCode);
        context.expectEq("resetStats preserves health count", healthAfterStatsReset.failureCount, failedHealth.failureCount);
#endif

        const IO::Types::Status shutdownStatus = Logger::shutdown();
        context.expectFalse("shutdown always stops Logger", Logger::isRunning());
        context.expectEq("shutdown health disabled", Logger::getHealth().state, Logger::Types::HealthState::Disabled);
        static_cast<void>(shutdownStatus);

        const Logger::Types::InitResult reinit = Logger::init(makeConsoleConfig());
        expectStarted(context, "health epoch reinit", reinit);
        const Logger::Types::HealthSnapshot reset = Logger::getHealth();
        context.expectEq("new init resets health state", reset.state, Logger::Types::HealthState::Healthy);
        context.expectEq("new init resets failure count", reset.failureCount, std::uint64_t{0});
        context.expectEq("new init resets failure source", reset.lastFailureSource, Logger::Types::FailureSource::None);
    }

    [[nodiscard]] TestSupport::Types::ChildProcessResult runChild(
        std::string_view executable,
        std::string_view argument,
        std::chrono::milliseconds timeout = 5s)
    {
        TestSupport::Types::ChildProcessOptions options;
        options.executablePath = std::filesystem::path(std::string(executable));
        options.arguments = {std::string(argument)};
        options.timeout = timeout;
        options.captureOutput = true;
        return TestSupport::runChildProcess(options);
    }

    [[nodiscard]] bool hasArgument(int argc, char **argv, std::string_view argument)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] == argument)
                return true;
        }
        return false;
    }

    int runFatalTerminateChild()
    {
#if defined(_WIN32)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
        Logger::Types::Config config;
        config.output = Logger::Types::OutputMode::None;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        if (const char *directory = std::getenv(std::string(childLogDirectoryEnvironmentVariable).c_str()))
        {
            config.output = Logger::Types::OutputMode::File;
            config.logDirectory = directory;
            config.fallbackToConsoleOnFileFailure = false;
            config.flushFileEveryBatch = true;
        }
        static_cast<void>(Logger::init(config));
        Logger::fatalTerminate(testSource, fatalTerminateChildMessage);
    }

    void testFatalTerminateChild(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableChildCrashTests)
        {
            context.pass("fatalTerminate child disabled by LoggerTestOptions");
            return;
        }

        const std::filesystem::path directory = testDirectory(context, "fatal-terminate-child");
        const TestSupport::ScopedEnvironmentVariable environment(childLogDirectoryEnvironmentVariable, pathText(directory));
        if (!environment.status().ok())
        {
            context.fail("set fatalTerminate child directory", TestSupport::formatInfrastructureStatus(environment.status()));
            return;
        }

        const TestSupport::Types::ChildProcessResult child = runChild(context.executablePath, fatalTerminateChildArgument);
        context.expectTrue(
            "fatalTerminate child exits nonzero",
            child.status.ok() && child.outcome == TestSupport::Types::ChildProcessOutcome::Exited && child.exitCode != 0);

        std::string contents;
        for (const auto &entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_regular_file())
                contents += readWholeFile(context, entry.path());
        }
        context.expectContains("fatalTerminate uses synchronous report", contents, fatalTerminateChildMessage);
    }

    void testManualFatalPopup(TestContext &context, const LoggerTestOptions &options)
    {
        if (!options.enableManualTests)
        {
            context.pass("manual Logger fatal popup skipped by LoggerTestOptions");
            return;
        }

        ScopedLoggerShutdown shutdown;
        Logger::Types::Config config = makeConsoleConfig();
        config.enableFatalPopup = true;
        expectStarted(context, "manual fatal popup init", Logger::init(config));
        static_cast<void>(Logger::reportFatal(testSource, "Manual Logger fatal popup test. Close this popup to continue."));
        context.pass("manual Logger fatal popup completed");
    }
} // namespace

namespace GameWIP::Test
{
    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options)
    {
        if (hasArgument(argc, argv, fatalTerminateChildArgument))
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
        const TestSupport::Types::SuiteResult suite = runner.runSuite(
            "Logger",
            [&](TestSupport::Context &suiteContext)
            {
                TestContext context(suiteContext);
                const TestSupport::ScopedTemporaryDirectory workspace("logger_tests");
                if (!workspace.status().ok())
                {
                    context.fail("create Logger test workspace", TestSupport::formatInfrastructureStatus(workspace.status()));
                    return;
                }

                context.logRoot = workspace.path();
                context.executablePath = argc > 0 && argv[0] != nullptr ? std::filesystem::absolute(argv[0]).string() : std::string{};
                const TestSupport::ScopedCurrentPath currentPath(workspace.path());
                if (!currentPath.status().ok())
                {
                    context.fail("set Logger test working directory", TestSupport::formatInfrastructureStatus(currentPath.status()));
                    return;
                }

                static_cast<void>(Logger::shutdown());
                runCase(
                    context,
                    "init result model",
                    [&]
                    {
                        testInitResultModel(context);
                    });
                runCase(
                    context,
                    "file fallback and setup status",
                    [&]
                    {
                        testFileFallbackAndSetupStatus(context);
                    });
                runCase(
                    context,
                    "file logging and UTF-8 truncation",
                    [&]
                    {
                        testFileLoggingAndUtf8Truncation(context);
                    });
                runCase(
                    context,
                    "filter statuses and concurrency",
                    [&]
                    {
                        testFilterStatusesAndConcurrency(context, options);
                    });
                runCase(
                    context,
                    "reports and health",
                    [&]
                    {
                        testReportsAndHealth(context);
                    });
                runCase(
                    context,
                    "report does not drain async backlog",
                    [&]
                    {
                        testReportDoesNotDrainAsyncBacklog(context);
                    });
                runCase(
                    context,
                    "flush contracts",
                    [&]
                    {
                        testFlushContracts(context);
                    });
                runCase(
                    context,
                    "fatal popup failure",
                    [&]
                    {
                        testFatalPopupFailure(context);
                    });
                runCase(
                    context,
                    "shutdown and health epoch",
                    [&]
                    {
                        testShutdownAndHealthEpoch(context);
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
                    "manual fatal popup",
                    [&]
                    {
                        testManualFatalPopup(context, options);
                    });
                static_cast<void>(Logger::shutdown());
            });

        runner.summary(
            std::format(
                "logger passed={} failed={} skipped={} elapsedMs={:.3f}",
                suite.summary.passed,
                suite.summary.failed,
                suite.summary.skipped,
                suite.elapsedMilliseconds));
        static_cast<void>(Logger::shutdown());
        return runner.exitCode();
    }
} // namespace GameWIP::Test
