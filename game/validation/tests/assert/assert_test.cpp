/// @file assert_test.cpp
/// @brief Executable self-tests for the Assert library.
///
/// The suite keeps shared fixtures and child routing in one translation unit while behavior-focused
/// private fragments cover macros, diagnostics, hooks, interactive actions, stress, process paths, and manual UI.

#include "validation/tests/assert/assert_test.h"

#include "debug/assert/assert.h"

#ifndef ASSERT_INTERNAL_TEST_HOOKS
#define ASSERT_INTERNAL_TEST_HOOKS 0
#endif

#if ASSERT_INTERNAL_TEST_HOOKS
#include "debug/assert/internal/assert_test_hooks.h"
#endif
#include "logger/logger.h"
#include "test_support/test_support.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
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
    namespace TestSupport = GameWIP::TestSupport;
    using AssertTestOptions = GameWIP::Test::AssertTestOptions;
    using namespace std::chrono_literals;

    constexpr std::string_view assertFailureChildArgument = "--assert-test-child=assert-failure";
    constexpr std::string_view debugBreakChildArgument = "--assert-test-child=debug-break";
    constexpr std::string_view unreachableChildArgument = "--assert-test-child=unreachable";
    constexpr std::string_view interactiveAbortChildArgument = "--assert-test-child=interactive-abort";
    constexpr std::string_view interactiveBreakChildArgument = "--assert-test-child=interactive-break";
    constexpr std::string_view suppressPopupEnvironmentVariable = "INTERNAL_ASSERT_SUPPRESS_POPUP";
    constexpr std::string_view testActionEnvironmentVariable = "INTERNAL_ASSERT_TEST_ACTION";
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "INTERNAL_ASSERT_TEST_CHILD_LOG_DIR";
    constexpr std::string_view assertFailureChildMessage = "assert child logger message";

    /// @brief Mutable test state and TestSupport-backed reporting for the Assert suite.
    struct TestContext
    {
        explicit TestContext(TestSupport::Context &testContext) noexcept
            : testContext(testContext)
        {
        }

        TestSupport::Context &testContext;
        std::filesystem::path logRoot;
        std::string executablePath;

        [[nodiscard]] TestSupport::Types::Reporting::Summary result() const noexcept
        {
            return testContext.result();
        }

        [[nodiscard]] bool ok() const noexcept
        {
            return testContext.ok();
        }

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
                return;
            }
            testContext.fail(name, details);
        }

        void expectFalse(std::string_view name, bool value, std::string_view details = "expected false")
        {
            if (!value)
            {
                testContext.pass(name);
                return;
            }
            testContext.fail(name, details);
        }

        template <typename Left, typename Right> void expectEq(std::string_view name, const Left &actual, const Right &expected)
        {
            static_cast<void>(testContext.expectEq(name, expected, actual));
        }

        void expectContains(std::string_view name, std::string_view text, std::string_view expectedSubstring)
        {
            static_cast<void>(testContext.expectContains(name, text, expectedSubstring));
        }

        void expectFileContains(std::string_view name, const std::filesystem::path &path, std::string_view expectedSubstring)
        {
            static_cast<void>(testContext.expectFileContains(name, path, expectedSubstring));
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
            Logger::shutdown();
            context.fail(name, exception.what());
        }
        catch (...)
        {
            Logger::shutdown();
            context.fail(name, "unknown exception");
        }
    }

    using ScopedEnvironmentVariable = TestSupport::ScopedEnvironmentVariable;
    using ScopedClearedEnvironmentVariable = TestSupport::ScopedUnsetEnvironmentVariable;

    bool requireInfrastructure(TestContext &context, std::string_view operation, const TestSupport::Types::InfrastructureStatus &status)
    {
        if (status.ok())
        {
            return true;
        }

        context.fail(operation, TestSupport::formatInfrastructureStatus(status));
        return false;
    }

    struct ScopedLoggerShutdown
    {
        ~ScopedLoggerShutdown()
        {
            Logger::shutdown();
        }
    };

    bool hasArgument(int argc, char **argv, std::string_view argument)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]) == argument)
            {
                return true;
            }
        }
        return false;
    }

    TestSupport::Types::Process::Result runChildProcessResult(
        std::string_view executablePath,
        std::string_view argument,
        std::chrono::milliseconds timeout = 5000ms)
    {
        TestSupport::Types::Process::Options child;
        child.executablePath = std::filesystem::path(std::string(executablePath));
        child.arguments = {std::string(argument)};
        child.timeout = timeout;
        child.captureOutput = true;
        return TestSupport::runChildProcess(child);
    }

    std::string pathText(const std::filesystem::path &path)
    {
        return path.generic_string();
    }

    std::string readFile(TestContext &context, const std::filesystem::path &path)
    {
        TestSupport::Types::TextResult result = TestSupport::readTextFile(path);
        if (!requireInfrastructure(context, "read Assert log fixture", result.status))
        {
            return {};
        }
        return std::move(result.text);
    }

    std::string readDirectoryFiles(TestContext &context, const std::filesystem::path &directory)
    {
        std::string contents;
        if (!std::filesystem::exists(directory))
        {
            return contents;
        }

        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_regular_file())
            {
                contents += readFile(context, entry.path());
            }
        }
        return contents;
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

    std::string makeDiagnosticMessage(int &evaluations)
    {
        ++evaluations;
        return "evaluated diagnostic message";
    }

    bool initFileLogger(TestContext &context, std::string_view name)
    {
        Logger::shutdown();
        const std::filesystem::path directory = context.logRoot / std::string(name);
        if (!TestSupport::createDirectories(directory).ok())
        {
            return false;
        }
        const std::string directoryText = pathText(directory);

        Logger::Types::Config config;
        config.output = Logger::Types::OutputMode::File;
        config.minLevel = Logger::Types::Level::Trace;
        config.logDirectory = directoryText;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        return Logger::init(config).status.ok();
    }

#include "validation/tests/assert/macro_behavior_test.inl"
#include "validation/tests/assert/diagnostics_test.inl"
#include "validation/tests/assert/test_hooks_test.inl"
#include "validation/tests/assert/interactive_test.inl"
#include "validation/tests/assert/stress_test.inl"
#include "validation/tests/assert/process_test.inl"
#include "validation/tests/assert/manual_test.inl"

    void printSummary(TestContext &context)
    {
        context.emit(
            std::format(
                "[RESULT] assert passed={} failed={} skipped={}\n",
                context.result().passed,
                context.result().failed,
                context.result().skipped));
    }
} // namespace

namespace GameWIP::Test
{
    int runAssertTests(int argc, char **argv, const AssertTestOptions &options)
    {
        if (hasArgument(argc, argv, assertFailureChildArgument))
        {
            return runAssertFailureChild();
        }
        if (hasArgument(argc, argv, interactiveAbortChildArgument))
        {
            return runInteractiveAbortChild();
        }
        if (hasArgument(argc, argv, interactiveBreakChildArgument))
        {
            return runInteractiveBreakChild();
        }
        if (hasArgument(argc, argv, debugBreakChildArgument))
        {
            return runDebugBreakChild();
        }
        if (hasArgument(argc, argv, unreachableChildArgument))
        {
            return runUnreachableChild();
        }

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("Assert test report: {}", options.writeReport ? options.reportPath.string() : std::string{"disabled"}));

        const TestSupport::Types::Reporting::SuiteResult suite = runner.runSuite(
            "Assert",
            [&](TestSupport::Context &suiteContext)
            {
                TestContext context(suiteContext);
                context.executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : "";
                const TestSupport::ScopedTemporaryDirectory workspace("assert_tests");
                if (!requireInfrastructure(context, "create Assert test workspace", workspace.status()))
                {
                    return;
                }
                const ScopedLoggerShutdown loggerShutdown;
                context.logRoot = workspace.path();

                context.emit(std::format("[INFO] Assert test log root: {}\n", pathText(context.logRoot)));
                context.emit(
                    std::format(
                        "[INFO] Assert config: runtime={} enabled={} checks={} diagnostics={} popupAssert={} popupCheck={}\n",
                        ASSERT_INTERNAL_RUNTIME,
                        ASSERT_ENABLED,
                        ASSERT_CHECKS_ENABLED,
                        ASSERT_DIAGNOSTICS,
                        ASSERT_POPUP_ON_ASSERT,
                        ASSERT_POPUP_ON_CHECK));
                context.emit(
                    std::format(
                        "[INFO] Assert test options: stress={} fatalChild={} automatedInteractive={} manualTests={} "
                        "stressThreads={} stressIterations={} report={}\n",
                        options.enableStressTests,
                        options.enableChildCrashTests,
                        options.enableAutomatedInteractiveTests,
                        options.enableManualTests,
                        options.stressThreadCount,
                        options.stressIterations,
                        options.writeReport ? options.reportPath.string() : std::string{"disabled"}));

                runCase(
                    context,
                    "passing macros",
                    [&]
                    {
                        testPassingMacros(context);
                    });
                runCase(
                    context,
                    "disabled macro evaluation",
                    [&]
                    {
                        testDisabledMacroEvaluation(context);
                    });
                runCase(
                    context,
                    "VERIFY evaluation",
                    [&]
                    {
                        testVerifyEvaluation(context);
                    });
                runCase(
                    context,
                    "ENSURE behavior",
                    [&]
                    {
                        testEnsureBehavior(context);
                    });
                runCase(
                    context,
                    "CHECK_ONCE logging",
                    [&]
                    {
                        testCheckOnceLogging(context);
                    });
                runCase(
                    context,
                    "diagnostic configuration",
                    [&]
                    {
                        testDiagnosticConfiguration(context);
                    });
                runCase(
                    context,
                    "diagnostic message evaluation",
                    [&]
                    {
                        testDiagnosticMessageEvaluation(context);
                    });
                runCase(
                    context,
                    "compiled-out message evaluation",
                    [&]
                    {
                        testCompiledOutMessageEvaluation(context);
                    });
                runCase(
                    context,
                    "UTF-8 diagnostic truncation",
                    [&]
                    {
                        testUtf8DiagnosticTruncation(context);
                    });
                runCase(
                    context,
                    "assert test hooks",
                    [&]
                    {
                        testAssertTestHooks(context);
                    });
                runCase(
                    context,
                    "automated interactive assert tests",
                    [&]
                    {
                        if (options.enableAutomatedInteractiveTests)
                        {
                            testInteractiveIgnoreOnce(context);
                            testInteractiveAlwaysIgnore(context);
                            testVerifyInteractiveEvaluation(context);
                            testVerifyInteractiveAlwaysIgnoreStillEvaluates(context);
                            testInteractiveStressLoops(context, options);
                            testInteractiveAbortChild(context, options);
                            testInteractiveBreakChild(context, options);
                        }
                        else
                        {
                            context.pass("automated interactive assert tests skipped by AssertTestOptions");
                        }
                    });
                runCase(
                    context,
                    "CHECK_ONCE thread stress",
                    [&]
                    {
                        testCheckOnceThreadStress(context, options);
                    });
                runCase(
                    context,
                    "ASSERT failure child",
                    [&]
                    {
                        testAssertFailureChild(context, options);
                    });
                runCase(
                    context,
                    "DEBUG_BREAK child",
                    [&]
                    {
                        testDebugBreakChild(context);
                    });
                runCase(
                    context,
                    "UNREACHABLE child",
                    [&]
                    {
                        testUnreachableChild(context);
                    });
                runCase(
                    context,
                    "manual assert UI",
                    [&]
                    {
                        testManualAssertUi(context, options);
                    });
                Logger::shutdown();
                printSummary(context);
            });

        runner.summary(
            std::format(
                "assert passed={} failed={} skipped={} elapsedMs={:.3f}",
                suite.summary.passed,
                suite.summary.failed,
                suite.summary.skipped,
                suite.elapsedMilliseconds));
        Logger::shutdown();
        return runner.exitCode();
    }
} // namespace GameWIP::Test
