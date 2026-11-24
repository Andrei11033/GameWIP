/// @file logger_test.cpp
/// @brief Executable self-tests for the Logger library.

#include "validation/tests/logger/logger_test.h"

#include "validation/executable_path.h"
#include "validation/platform_tests.h"
#include "validation/process_arguments.h"
#include "validation/tests/internal/path_text.h"

#include "logger/logger.h"
#include "logger/logger_macros.h"
#include "validation/tests/internal/helpers/runner_helpers.h"
#include "test_support/test_support.h"
#include "unicode/unicode.h"

#ifndef LOGGER_INTERNAL_TEST_HOOKS
#define LOGGER_INTERNAL_TEST_HOOKS 0
#endif

#if LOGGER_INTERNAL_TEST_HOOKS
#include "logger/internal/test_hooks.h"
#endif

#ifndef TERMINAL_INTERNAL_TEST_HOOKS
#define TERMINAL_INTERNAL_TEST_HOOKS 0
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
#include "terminal/internal/test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

/// ------------------------------------------------------------
/// Compile-time format probe
/// ------------------------------------------------------------

struct LoggerFormatProbe
{
    int value = 0;
    std::atomic<std::size_t> *formatCount = nullptr;
};

namespace std
{
    template <> struct formatter<LoggerFormatProbe, char> : formatter<int, char>
    {
        template <typename FormatContext> auto format(const LoggerFormatProbe &probe, FormatContext &context) const
        {
            if(probe.formatCount != nullptr)
            {
                probe.formatCount->fetch_add(1, memory_order_relaxed);
            }

            return formatter<int, char>::format(probe.value, context);
        }
    };
} // namespace std

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;
    using GameWIP::Test::LoggerTestOptions;
    using namespace std::chrono_literals;

    constexpr std::string_view kTestSource = "LoggerTest";
    constexpr std::string_view kChildLogDirectoryEnvironmentVariable = "LOGGER_INTERNAL_TEST_CHILD_LOG_DIR";
    constexpr std::string_view kFatalTerminateChildArgument = "--logger-test-child=fatal-terminate";
    constexpr std::string_view kFatalTerminateChildMessage = "child fatal terminate";

    enum class TestSource : Logger::Types::SourceId
    {
        Core = 1,
        Render = 2,
        Unknown = 99
    };

    enum class SignedSource : int
    {
        Core = 1
    };

    static_assert(Logger::SourceEnum<TestSource>);
    static_assert(!Logger::SourceEnum<SignedSource>);
    static_assert(!Logger::SourceEnum<Logger::Types::Level>);

#include "validation/tests/logger/logger_test_support.inl"

#include "validation/tests/logger/configuration_test.inl"
#include "validation/tests/logger/output_test.inl"
#include "validation/tests/logger/record_layout_test.inl"
#include "validation/tests/logger/filters_test.inl"
#include "validation/tests/logger/reports_test.inl"
#include "validation/tests/logger/health_test.inl"
#include "validation/tests/logger/process_test.inl"

} // namespace

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    int runLoggerTests(int argc, char **argv, const LoggerTestOptions &options)
    {
        if(hasArgument(argc, argv, "--logger-test-child=report-no-drain"))
        {
            return runReportNoDrainChild();
        }

        if(hasArgument(argc, argv, kFatalTerminateChildArgument))
        {
            return runFatalTerminateChild();
        }

        const TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        const TestSupport::Types::Reporting::SuiteResult suite = runner.runSuite(
            "Logger",
            [&](TestSupport::Context &suiteContext)
            {
                TestContext context(suiteContext);

                if(!context.processState.status().ok())
                {
                    context.fail("acquire Logger process-state scope", TestSupport::formatInfrastructureStatus(context.processState.status()));
                    return;
                }

                const TestSupport::ScopedTemporaryDirectory workspace("logger_tests");

                if(!workspace.status().ok())
                {
                    context.fail("create Logger test workspace", TestSupport::formatInfrastructureStatus(workspace.status()));
                    return;
                }
                context.logRoot = workspace.path();
                context.executablePath = GameWIP::Validation::currentExecutablePath();
                const TestSupport::ScopedCurrentPath currentPath(context.processState, workspace.path());

                if(!currentPath.status().ok())
                {
                    context.fail("set Logger test working directory", TestSupport::formatInfrastructureStatus(currentPath.status()));
                    return;
                }

                static_cast<void>(Logger::shutdown());

                // Check initialization and file output.
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
                    "configurable timestamp formatting",
                    [&]
                    {
                        testConfigurableTimestampFormatting(context);
                    });

                // Check structured records and layouts.
                runCase(
                    context,
                    "marked values and record fields",
                    [&]
                    {
                        testMarkedValuesAndRecordFields(context);
                    });
                runCase(
                    context,
                    "custom record layouts and ownership",
                    [&]
                    {
                        testCustomRecordLayoutsAndOwnership(context);
                    });
                runCase(
                    context,
                    "record field limits",
                    [&]
                    {
                        testRecordFieldLimits(context);
                    });
                runCase(
                    context,
                    "Record formatting honors message bounds",
                    [&]
                    {
                        testRecordFormattingHonorsMessageBound(context);
                    });
                runCase(
                    context,
                    "layout alignment",
                    [&]
                    {
                        testLayoutAlignment(context);
                    });
                runCase(
                    context,
                    "truncated compiled layout fallback",
                    [&]
                    {
                        testTruncatedCompiledLayoutFallsBackSafely(context);
                    });
                runCase(
                    context,
                    "console theme and area colors",
                    [&]
                    {
                        testConsoleThemeAndAreaColors(context);
                    });

                // Check filters, reports, and flush outcomes.
                runCase(
                    context,
                    "filtered formatted registered source",
                    [&]
                    {
                        testFilteredFormattedRegisteredSource(context);
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
                        testReportDoesNotDrainAsyncBacklog(context, options);
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
                    "query defaults and lifecycle",
                    [&]
                    {
                        testQueryDefaultsAndLifecycle(context);
                    });
                runCase(
                    context,
                    "shutdown and health epoch",
                    [&]
                    {
                        testShutdownAndHealthEpoch(context);
                    });

                // Check child-process and manual fatal paths.
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

                if(options.enableManualTests)
                {
                    context.testContext.summary(
                        std::format(
                            "Logger manual observations passed={} failed={} skipped={} (excludes setup assertions)",
                            context.manualObservations.passed,
                            context.manualObservations.failed,
                            context.manualObservations.skipped));
                }
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
        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
