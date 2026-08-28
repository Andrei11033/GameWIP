/// @file logger_test.cpp
/// @brief Executable self-tests for the Logger library.

#include "validation/tests/logger/logger_test.h"
#include "validation/process_arguments.h"

#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "test_support/test_support.h"
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
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "LOGGER_INTERNAL_TEST_CHILD_LOG_DIR";
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
        explicit TestContext(TestSupport::Context &context) noexcept
            : testContext(context)
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

    // ------------------------------------------------------------
    // Test helpers and fixtures
    // ------------------------------------------------------------

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

    [[nodiscard]] bool reportDelivered(const Logger::Types::Report::Result &result) noexcept
    {
        return result.status.ok() && result.outcome == Logger::Types::Report::Outcome::Completed &&
               result.delivery != Logger::Types::Report::Delivery::None;
    }

    [[nodiscard]] std::string pathText(const std::filesystem::path &path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    }

    [[nodiscard]] std::filesystem::path pathFromText(std::string_view text)
    {
        std::u8string converted(text.size(), u8'\0');
        std::ranges::transform(
            text,
            converted.begin(),
            [](char byte)
            {
                return static_cast<char8_t>(byte);
            });
        return std::filesystem::path(converted);
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

#include "validation/tests/logger/configuration_test.inl"
#include "validation/tests/logger/output_test.inl"
#include "validation/tests/logger/filters_test.inl"
#include "validation/tests/logger/reports_test.inl"
#include "validation/tests/logger/health_test.inl"
#include "validation/tests/logger/process_test.inl"

} // namespace

namespace GameWIP::Test
{
    // ------------------------------------------------------------
    // Test runner
    // ------------------------------------------------------------

    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options)
    {
        if (hasArgument(argc, argv, fatalTerminateChildArgument))
        {
            return runFatalTerminateChild();
        }

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        const TestSupport::Types::Reporting::SuiteResult suite = runner.runSuite(
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
