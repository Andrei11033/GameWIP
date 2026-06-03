/// @file assert_test.cpp
/// @brief Executable self-tests for the GameWIP Assert library.

#include "test/assert_test.h"

#include "debug/assert/assert.h"

#ifndef GAMEWIP_ASSERT_TEST_HOOKS
#define GAMEWIP_ASSERT_TEST_HOOKS 0
#endif

#if GAMEWIP_ASSERT_TEST_HOOKS
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
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
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
    namespace TestSupport = GameWIP::TestSupport;
    using AssertTestOptions = GameWIP::Test::AssertTestOptions;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::string_view assertFailureChildArgument = "--assert-test-child=assert-failure";
    constexpr std::string_view debugBreakChildArgument = "--assert-test-child=debug-break";
    constexpr std::string_view unreachableChildArgument = "--assert-test-child=unreachable";
    constexpr std::string_view interactiveAbortChildArgument = "--assert-test-child=interactive-abort";
    constexpr std::string_view interactiveBreakChildArgument = "--assert-test-child=interactive-break";
    constexpr std::string_view suppressPopupEnvironmentVariable = "GAMEWIP_ASSERT_SUPPRESS_POPUP";
    constexpr std::string_view testActionEnvironmentVariable = "GAMEWIP_ASSERT_TEST_ACTION";
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "GAMEWIP_ASSERT_TEST_CHILD_LOG_DIR";
    constexpr std::string_view assertFailureChildMessage = "assert child logger message";

    std::size_t performanceSink = 0;

    struct ScopedLogRootCleanup
    {
        explicit ScopedLogRootCleanup(const std::filesystem::path &path)
            : path(path)
        {
        }

        ~ScopedLogRootCleanup() noexcept
        {
            Logger::shutdown();
            try
            {
                TestSupport::removeIfExists(path);
            }
            catch (...)
            {
            }
        }

        ScopedLogRootCleanup(const ScopedLogRootCleanup &) = delete;
        ScopedLogRootCleanup &operator=(const ScopedLogRootCleanup &) = delete;

        std::filesystem::path path;
    };

    /// @brief Mutable test state and TestSupport-backed reporting for the assert suite.
    struct TestContext
    {
        explicit TestContext(TestSupport::Context &testContext) noexcept
            : testContext(testContext)
        {
        }

        TestSupport::Context &testContext;
        std::filesystem::path logRoot;
        std::string executablePath;
        double performanceMilliseconds = 0.0;
        std::size_t performanceScenarioCount = 0;

        [[nodiscard]] TestSupport::Types::Summary result() const noexcept
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

        /// @brief Records a passed assertion-test scenario.
        /// @param name Scenario name.
        void pass(std::string_view name)
        {
            testContext.pass(name);
        }

        /// @brief Records a failed assertion-test scenario.
        /// @param name Scenario name.
        /// @param details Failure details.
        void fail(std::string_view name, std::string_view details)
        {
            testContext.fail(name, details);
        }

        /// @brief Expects a boolean value to be true.
        /// @param name Scenario name.
        /// @param value Value to inspect.
        /// @param details Failure details.
        void expectTrue(std::string_view name, bool value, std::string_view details = "expected true")
        {
            if (value)
            {
                testContext.pass(name);
                return;
            }
            testContext.fail(name, details);
        }

        /// @brief Expects a boolean value to be false.
        /// @param name Scenario name.
        /// @param value Value to inspect.
        /// @param details Failure details.
        void expectFalse(std::string_view name, bool value, std::string_view details = "expected false")
        {
            if (!value)
            {
                testContext.pass(name);
                return;
            }
            testContext.fail(name, details);
        }

        /// @brief Expects two values to compare equal.
        /// @param name Scenario name.
        /// @param actual Actual value.
        /// @param expected Expected value.
        template <typename Left, typename Right>
        void expectEq(std::string_view name, const Left &actual, const Right &expected)
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

    template <typename Function>
    void runCase(TestContext &context, std::string_view name, Function &&function)
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

    /// @brief Stops the logger when a test scope exits.
    struct ScopedLoggerShutdown
    {
        ~ScopedLoggerShutdown()
        {
            Logger::shutdown();
        }
    };

    /// @brief Returns true when the process was launched with an exact argument.
    /// @param argc Process argument count.
    /// @param argv Process argument values.
    /// @param argument Argument to search for.
    /// @return True when the argument is present.
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

    /// @brief Runs a child process through TestSupport.
    /// @param executablePath Executable to launch.
    /// @param argument Single child-mode argument to pass.
    /// @return Child process result with exit, timeout, and captured output state.
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

    /// @brief Builds a unique temporary log root for one assert test run.
    /// @return Filesystem path for assert test logs.
    std::filesystem::path makeRunRoot()
    {
        const auto ticks = Clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / std::format("GameWIP_assert_tests_{}", ticks);
    }

    /// @brief Converts a path to the narrow text form expected by Logger::Config.
    /// @param path Filesystem path to convert.
    /// @return Narrow path text.
    std::string pathText(const std::filesystem::path &path)
    {
        return path.generic_string();
    }

    /// @brief Reads an entire text file.
    /// @param path File path to read.
    /// @return File contents, or empty text when the file cannot be opened.
    std::string readFile(const std::filesystem::path &path)
    {
        return TestSupport::readTextFile(path);
    }

    /// @brief Reads and concatenates every regular file in a directory.
    /// @param directory Directory to scan.
    /// @return Concatenated file contents.
    std::string readDirectoryFiles(const std::filesystem::path &directory)
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
                contents += readFile(entry.path());
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

    /// @brief Returns a message string and records that it was evaluated.
    /// @param evaluations Evaluation counter to increment.
    /// @return Diagnostic message text.
    std::string makeDiagnosticMessage(int &evaluations)
    {
        ++evaluations;
        return "evaluated diagnostic message";
    }

    /// @brief Starts a file-only logger for assert tests that inspect flushed CHECK output.
    /// @param context Test context with the log root.
    /// @param name Per-scenario log subdirectory name.
    /// @return True when logger initialization succeeded.
    bool initFileLogger(TestContext &context, std::string_view name)
    {
        Logger::shutdown();
        const std::filesystem::path directory = context.logRoot / std::string(name);
        TestSupport::createDirectories(directory);
        const std::string directoryText = pathText(directory);

        Logger::Types::Config config;
        config.output = Logger::Types::Output::File;
        config.minLevel = Logger::Types::Level::Trace;
        config.logDirectory = directoryText;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        return Logger::init(config) == Logger::Types::Result::Success;
    }

    /// @brief Exercises passing macros and statement safety.
    /// @param context Test context.
    void testPassingMacros(TestContext &context)
    {
        int value = 0;

        if (true)
            ASSERT(true);
        else
            ++value;

        ASSERT_MSG(value == 0, "passing assert message");
        CHECK(true);
        CHECK_MSG(value == 0, "passing check message");
        CHECK_ONCE(true);
        CHECK_ONCE_MSG(true, "passing check once message");

        context.expectEq("passing macros keep value", value, 0);
    }

    /// @brief Verifies disabled reporting macros do not evaluate expressions with side effects.
    /// @param context Test context.
    void testDisabledMacroEvaluation(TestContext &context)
    {
        int assertEvaluations = 0;
        ASSERT(++assertEvaluations == 1);
        ASSERT_MSG(++assertEvaluations == 2, "disabled assert message");

#if GAMEWIP_ASSERT_ENABLED
        context.expectEq("ASSERT macros evaluate when enabled", assertEvaluations, 2);
#else
        context.expectEq("ASSERT macros skip expressions when disabled", assertEvaluations, 0);
#endif

        int checkEvaluations = 0;
        CHECK(++checkEvaluations == 1);
        CHECK_MSG(++checkEvaluations == 2, "disabled check message");
        CHECK_ONCE(++checkEvaluations == 3);
        CHECK_ONCE_MSG(++checkEvaluations == 4, "disabled check once message");

#if GAMEWIP_ASSERT_CHECKS_ENABLED
        context.expectEq("CHECK macros evaluate when enabled", checkEvaluations, 4);
#else
        context.expectEq("CHECK macros skip expressions when disabled", checkEvaluations, 0);
#endif
    }

    /// @brief Verifies that VERIFY evaluates its expression even when assertion reporting is disabled.
    /// @param context Test context.
    void testVerifyEvaluation(TestContext &context)
    {
        int evaluations = 0;
        VERIFY(++evaluations == 1);
        VERIFY_MSG(++evaluations == 2, "verify message");
        context.expectEq("VERIFY evaluates expressions", evaluations, 2);
    }

    /// @brief Verifies that ENSURE evaluates once and returns the condition result.
    /// @param context Test context.
    void testEnsureBehavior(TestContext &context)
    {
        ScopedLoggerShutdown loggerShutdown;
        initFileLogger(context, "ensure");

        int evaluations = 0;
        const bool first = ENSURE(++evaluations == 1);
        const bool second = ENSURE_MSG(++evaluations == 3, "ensure false message");

        context.expectTrue("ENSURE true result", first);
        context.expectTrue("ENSURE false result", !second, "expected false");
        context.expectEq("ENSURE evaluates once per call", evaluations, 2);

#if GAMEWIP_ASSERT_CHECKS_ENABLED
        const std::string contents = readFile(Logger::getLogFilePath());
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("ENSURE diagnostics include caller function", contents.find("testEnsureBehavior") != std::string::npos, "caller function missing");
        context.expectTrue("ENSURE diagnostics avoid lambda function", contents.find("operator()") == std::string::npos, "lambda function leaked into diagnostics");
#else
        context.expectTrue("ENSURE diagnostics stripped message", contents.find("ensure false message") == std::string::npos, "diagnostic message was embedded");
#endif
#endif
    }

    /// @brief Verifies that CHECK_ONCE reports only one flushed log entry per call site.
    /// @param context Test context.
    void testCheckOnceLogging(TestContext &context)
    {
#if GAMEWIP_ASSERT_CHECKS_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "check_once"))
        {
            context.fail("CHECK_ONCE logger init", "Logger::init failed");
            return;
        }

        Logger::resetStats();
        for (int index = 0; index < 3; ++index)
        {
            CHECK_ONCE(false);
        }
        Logger::flush(2s);
        const Logger::Types::Stats stats = Logger::getStats();
        const std::string contents = readFile(Logger::getLogFilePath());
        context.expectEq("CHECK_ONCE reports without queueing", stats.queued, std::size_t{0});
        context.expectEq("CHECK_ONCE writes one failure synchronously", stats.written, std::size_t{1});
        context.expectTrue("CHECK_ONCE log contains error failure", contents.find("[ERROR][Check]: Check failed") != std::string::npos, "check failure missing from log");
#else
        context.pass("CHECK_ONCE logger test skipped because GAMEWIP_ASSERT_CHECKS_ENABLED=0");
#endif
    }

    /// @brief Verifies diagnostic text is present or intentionally stripped according to GAMEWIP_ASSERT_DIAGNOSTICS.
    /// @param context Test context.
    void testDiagnosticConfiguration(TestContext &context)
    {
#if GAMEWIP_ASSERT_CHECKS_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "diagnostics"))
        {
            context.fail("diagnostics logger init", "Logger::init failed");
            return;
        }

        CHECK_MSG(false, "assert diagnostic message");
        Logger::flush(2s);
        const std::string contents = readFile(Logger::getLogFilePath());

#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("diagnostics include condition", contents.find("false") != std::string::npos, "condition text missing");
        context.expectTrue("diagnostics include message", contents.find("assert diagnostic message") != std::string::npos, "custom message missing");
        context.expectTrue("diagnostics include location", contents.find("assert_test.cpp") != std::string::npos, "file text missing");
        context.expectTrue("diagnostics include caller function", contents.find("testDiagnosticConfiguration") != std::string::npos, "function text missing");
#else
        context.expectTrue("diagnostics stripped condition", contents.find("false") == std::string::npos, "condition text was embedded");
        context.expectTrue("diagnostics stripped message", contents.find("assert diagnostic message") == std::string::npos, "diagnostic message was embedded");
        context.expectTrue("diagnostics stripped location", contents.find("assert_test.cpp") == std::string::npos, "location text was embedded");
#endif
#else
        context.pass("diagnostic logger test skipped because GAMEWIP_ASSERT_CHECKS_ENABLED=0");
#endif
    }

    /// @brief Verifies diagnostic messages are only evaluated when diagnostics are compiled in.
    /// @param context Test context.
    void testDiagnosticMessageEvaluation(TestContext &context)
    {
#if GAMEWIP_ASSERT_CHECKS_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "diagnostic_message_evaluation"))
        {
            context.fail("diagnostic message evaluation logger init", "Logger::init failed");
            return;
        }

        int evaluations = 0;
        CHECK_MSG(false, makeDiagnosticMessage(evaluations));
        const std::string contents = readFile(Logger::getLogFilePath());

#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectEq("diagnostic message evaluated when enabled", evaluations, 1);
        context.expectTrue("diagnostic evaluated message logged", contents.find("evaluated diagnostic message") != std::string::npos, "evaluated message missing");
#else
        context.expectEq("diagnostic message skipped when stripped", evaluations, 0);
        context.expectTrue("diagnostic evaluated message stripped", contents.find("evaluated diagnostic message") == std::string::npos, "diagnostic message was embedded");
#endif
#else
        context.pass("diagnostic message evaluation skipped because GAMEWIP_ASSERT_CHECKS_ENABLED=0");
#endif
    }

    /// @brief Verifies message expressions are skipped when macro families are compiled out.
    /// @param context Test context.
    void testCompiledOutMessageEvaluation(TestContext &context)
    {
        int evaluations = 0;

#if !GAMEWIP_ASSERT_ENABLED
        ASSERT_MSG(false, makeDiagnosticMessage(evaluations));
        ASSERT_INTERACTIVE_MSG(false, makeDiagnosticMessage(evaluations));
        VERIFY_MSG(false, makeDiagnosticMessage(evaluations));
        VERIFY_INTERACTIVE_MSG(false, makeDiagnosticMessage(evaluations));
        context.expectEq("compiled-out assert messages skipped", evaluations, 0);
#else
        context.pass("compiled-out assert message test skipped because GAMEWIP_ASSERT_ENABLED=1");
#endif

#if !GAMEWIP_ASSERT_CHECKS_ENABLED
        CHECK_MSG(false, makeDiagnosticMessage(evaluations));
        CHECK_ONCE_MSG(false, makeDiagnosticMessage(evaluations));
        (void)ENSURE_MSG(false, makeDiagnosticMessage(evaluations));
        context.expectEq("compiled-out check messages skipped", evaluations, 0);
#else
        context.pass("compiled-out check message test skipped because GAMEWIP_ASSERT_CHECKS_ENABLED=1");
#endif
    }

    void interactiveAlwaysIgnoreSite()
    {
        ASSERT_INTERACTIVE_MSG(false, "interactive always ignore test");
    }

    void interactiveIgnoreOnceSite()
    {
        ASSERT_INTERACTIVE_MSG(false, "interactive ignore once repeat test");
    }

    void verifyInteractiveAlwaysIgnoreSite(int &evaluations)
    {
        VERIFY_INTERACTIVE_MSG(++evaluations < 0, "verify interactive always ignore test");
    }

    void threadedCheckOnceSite()
    {
        CHECK_ONCE_MSG(false, "threaded check once stress");
    }

    void testAssertTestHooks(TestContext &context)
    {
#if GAMEWIP_ASSERT_TEST_HOOKS
        using GameWIP::Debug::Assert::FailureAction;
        namespace AssertHooks = GameWIP::Debug::Assert::TestHooks;

        AssertHooks::reset();
        AssertHooks::setDebuggerAttachedOverride(true);
        context.expectTrue("hook debugger attached override true", AssertHooks::debuggerAttachedForTest());
        AssertHooks::setDebuggerAttachedOverride(false);
        context.expectTrue("hook debugger attached override false", !AssertHooks::debuggerAttachedForTest());
        AssertHooks::clearDebuggerAttachedOverride();

        AssertHooks::forceNextActionDialogFailure();
        AssertHooks::forceNextFallbackActionDialogFailure();
        const FailureAction fallbackAction = AssertHooks::showFailureActionDialogForTest(
            "Assert hook test",
            "Primary and fallback action dialogs are forced to fail; default action should be returned.",
            FailureAction::IgnoreOnce);
        context.expectTrue("hook action dialog failure consumed", !AssertHooks::Detail::consumeNextActionDialogFailure());
        context.expectTrue("hook fallback action dialog failure consumed", !AssertHooks::Detail::consumeNextFallbackActionDialogFailure());
        context.expectTrue("hook action dialog default fallback", fallbackAction == FailureAction::IgnoreOnce);

        AssertHooks::setPopupSuppressedOverride(true);
        AssertHooks::showErrorPopupForTest("Assert hook popup suppression", "This popup should be suppressed by the test hook.");
        context.pass("hook popup suppression override returned without UI");
        AssertHooks::reset();
#else
        context.pass("assert test hooks skipped because GAMEWIP_ASSERT_TEST_HOOKS=0");
#endif
    }

    void testInteractiveIgnoreOnce(TestContext &context)
    {
#if GAMEWIP_ASSERT_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "interactive_ignore_once"))
        {
            context.fail("interactive ignore once logger init", "Logger::init failed");
            return;
        }

        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "ignore_once");
        ASSERT_INTERACTIVE_MSG(false, "interactive ignore once test");

        Logger::flush(2s);
        const Logger::Types::Stats stats = Logger::getStats();
        const std::string contents = readFile(Logger::getLogFilePath());
        context.expectEq("ASSERT_INTERACTIVE ignore_once not queued", stats.queued, std::size_t{0});
        context.expectEq("ASSERT_INTERACTIVE ignore_once writes one fatal", stats.written, std::size_t{1});
        context.expectTrue("ASSERT_INTERACTIVE ignore_once logs fatal", contents.find("[FATAL][Assert]: Assert failed") != std::string::npos, "interactive fatal missing");
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("ASSERT_INTERACTIVE ignore_once logs message", contents.find("interactive ignore once test") != std::string::npos, "interactive message missing");
#else
        context.expectTrue("ASSERT_INTERACTIVE ignore_once strips message", contents.find("interactive ignore once test") == std::string::npos, "interactive message was embedded");
#endif
#else
        context.pass("ASSERT_INTERACTIVE ignore_once skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    void testInteractiveAlwaysIgnore(TestContext &context)
    {
#if GAMEWIP_ASSERT_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "interactive_always_ignore"))
        {
            context.fail("interactive always ignore logger init", "Logger::init failed");
            return;
        }

        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "always_ignore");
        interactiveAlwaysIgnoreSite();
        interactiveAlwaysIgnoreSite();

        Logger::flush(2s);
        const Logger::Types::Stats stats = Logger::getStats();
        const std::string contents = readFile(Logger::getLogFilePath());
        context.expectEq("ASSERT_INTERACTIVE always_ignore not queued", stats.queued, std::size_t{0});
        context.expectEq("ASSERT_INTERACTIVE always_ignore writes once", stats.written, std::size_t{1});
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectEq("ASSERT_INTERACTIVE always_ignore one message", countOccurrences(contents, "interactive always ignore test"), std::size_t{1});
#else
        context.expectEq("ASSERT_INTERACTIVE always_ignore strips message", countOccurrences(contents, "interactive always ignore test"), std::size_t{0});
#endif
#else
        context.pass("ASSERT_INTERACTIVE always_ignore skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    void testVerifyInteractiveEvaluation(TestContext &context)
    {
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "verify_interactive"))
        {
            context.fail("verify interactive logger init", "Logger::init failed");
            return;
        }

        int passingEvaluations = 0;
        VERIFY_INTERACTIVE(++passingEvaluations == 1);
        context.expectEq("VERIFY_INTERACTIVE passing evaluates once", passingEvaluations, 1);

        int failingEvaluations = 0;
        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "ignore_once");
        VERIFY_INTERACTIVE_MSG(++failingEvaluations < 0, "verify interactive ignore once test");
        context.expectEq("VERIFY_INTERACTIVE failing evaluates once", failingEvaluations, 1);

#if GAMEWIP_ASSERT_ENABLED
        Logger::flush(2s);
        const std::string contents = readFile(Logger::getLogFilePath());
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("VERIFY_INTERACTIVE failure logs when enabled", contents.find("verify interactive ignore once test") != std::string::npos, "verify interactive message missing");
#else
        context.expectTrue("VERIFY_INTERACTIVE failure strips message", contents.find("verify interactive ignore once test") == std::string::npos, "verify interactive message was embedded");
#endif
#else
        Logger::flush(2s);
        const Logger::Types::Stats stats = Logger::getStats();
        context.expectEq("VERIFY_INTERACTIVE disabled does not report", stats.written, std::size_t{0});
#endif
    }

    void testVerifyInteractiveAlwaysIgnoreStillEvaluates(TestContext &context)
    {
#if GAMEWIP_ASSERT_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "verify_interactive_always_ignore"))
        {
            context.fail("verify interactive always ignore logger init", "Logger::init failed");
            return;
        }

        int evaluations = 0;
        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "always_ignore");
        verifyInteractiveAlwaysIgnoreSite(evaluations);
        verifyInteractiveAlwaysIgnoreSite(evaluations);

        Logger::flush(2s);
        const std::string contents = readFile(Logger::getLogFilePath());
        context.expectEq("VERIFY_INTERACTIVE Always Ignore still evaluates", evaluations, 2);
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectEq("VERIFY_INTERACTIVE Always Ignore logs once", countOccurrences(contents, "verify interactive always ignore test"), std::size_t{1});
#else
        context.expectEq("VERIFY_INTERACTIVE Always Ignore strips message", countOccurrences(contents, "verify interactive always ignore test"), std::size_t{0});
#endif
#else
        context.pass("VERIFY_INTERACTIVE Always Ignore test skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    void testCheckOnceThreadStress(TestContext &context, const AssertTestOptions &options)
    {
#if GAMEWIP_ASSERT_CHECKS_ENABLED
        if (!options.enableStressTests)
        {
            context.pass("CHECK_ONCE thread stress skipped by AssertTestOptions");
            return;
        }

        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "check_once_thread_stress"))
        {
            context.fail("CHECK_ONCE thread stress logger init", "Logger::init failed");
            return;
        }

        const int threadCount = static_cast<int>(std::max<std::size_t>(2, options.stressThreadCount));
        std::atomic<bool> start{false};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threadCount));

        for (int index = 0; index < threadCount; ++index)
        {
            workers.emplace_back(
                [&start]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                    threadedCheckOnceSite();
                });
        }

        start.store(true, std::memory_order_release);
        for (std::thread &worker : workers)
        {
            worker.join();
        }

        Logger::flush(2s);
        const std::string contents = readFile(Logger::getLogFilePath());
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectEq("CHECK_ONCE thread stress logs once", countOccurrences(contents, "threaded check once stress"), std::size_t{1});
#else
        context.expectEq("CHECK_ONCE thread stress logs generic once", countOccurrences(contents, "[ERROR][Check]: Check failed"), std::size_t{1});
#endif
#else
        context.pass("CHECK_ONCE thread stress skipped because GAMEWIP_ASSERT_CHECKS_ENABLED=0");
#endif
    }

    void testInteractiveStressLoops(TestContext &context, const AssertTestOptions &options)
    {
#if GAMEWIP_ASSERT_ENABLED
        if (!options.enableStressTests)
        {
            context.pass("interactive assert stress loops skipped by AssertTestOptions");
            return;
        }

        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "interactive_stress_loops"))
        {
            context.fail("interactive stress logger init", "Logger::init failed");
            return;
        }

        const int iterations = static_cast<int>(std::max<std::size_t>(1, options.stressIterations));
        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "ignore_once");
        for (int index = 0; index < iterations; ++index)
        {
            interactiveIgnoreOnceSite();
        }

        Logger::flush(5s);
        const std::string contents = readFile(Logger::getLogFilePath());
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectEq("ASSERT_INTERACTIVE ignore_once stress logs every failure", countOccurrences(contents, "interactive ignore once repeat test"), static_cast<std::size_t>(iterations));
#else
        context.expectEq("ASSERT_INTERACTIVE ignore_once stress logs generic failures", countOccurrences(contents, "[FATAL][Assert]: Assert failed"), static_cast<std::size_t>(iterations));
#endif
#else
        context.pass("interactive assert stress loops skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    /// @brief Child-process body that intentionally triggers a failed ASSERT.
    /// @return Zero only if the assertion was compiled out or execution continued after the break.
    int runAssertFailureChild()
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
        }
        Logger::init(config);
        ASSERT_MSG(false, assertFailureChildMessage);
        Logger::shutdown();
        return 0;
    }

    int runInteractiveAbortChild()
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
        }
        Logger::init(config);
        ASSERT_INTERACTIVE_MSG(false, "interactive abort child");
        Logger::shutdown();
        return 0;
    }

    int runInteractiveBreakChild()
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
        }
        Logger::init(config);
        ASSERT_INTERACTIVE_MSG(false, "interactive break child");
        Logger::shutdown();
        return 0;
    }

    /// @brief Child-process body that intentionally triggers DEBUG_BREAK.
    /// @return Zero only if execution continues after the break.
    int runDebugBreakChild()
    {
#if defined(_WIN32)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
        DEBUG_BREAK();
        return 0;
    }

    /// @brief Child-process body that intentionally triggers UNREACHABLE when assertions are enabled.
    /// @return Zero only if UNREACHABLE is compiled out or execution continues after the break.
    int runUnreachableChild()
    {
#if defined(_WIN32)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
        UNREACHABLE();
        return 0;
    }

    /// @brief Runs a child process and expects it to exit abnormally.
    /// @param context Test context.
    /// @param argument Child-process argument.
    /// @param testName Test name.
    void expectAbnormalChildExit(TestContext &context, std::string_view argument, std::string_view testName)
    {
        std::cout.flush();
        std::cerr.flush();
        const TestSupport::Types::ChildProcessResult result = runChildProcessResult(context.executablePath, argument);
        context.expectTrue(testName, result.exitedWithFailure(), "child process returned zero");
    }

    /// @brief Verifies a failed ASSERT triggers an abnormal child-process exit when assertions are enabled.
    /// @param context Test context.
    /// @param options Assert test options.
    void testAssertFailureChild(TestContext &context, const AssertTestOptions &options)
    {
#if GAMEWIP_ASSERT_ENABLED
        if (!options.enableChildCrashTests)
        {
            context.pass("ASSERT failure child test disabled by AssertTestOptions");
            return;
        }

        const std::filesystem::path childLogDirectory = context.logRoot / "assert_child";
        TestSupport::createDirectories(childLogDirectory);
        const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
        const ScopedEnvironmentVariable suppressPopupOverride(suppressPopupEnvironmentVariable, "1");

        expectAbnormalChildExit(context, assertFailureChildArgument, "ASSERT failure child exits abnormally with popup suppressed");

        const std::string childLogContents = readDirectoryFiles(childLogDirectory);
        context.expectTrue("ASSERT failure child logs fatal through Logger", childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos, "assert failure missing from child log");
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("ASSERT failure child logs diagnostic message", childLogContents.find(assertFailureChildMessage) != std::string::npos, "child assert message missing from log");
#else
        context.expectTrue("ASSERT failure child strips diagnostic message", childLogContents.find(assertFailureChildMessage) == std::string::npos, "child assert message was embedded");
#endif
#else
        context.pass("ASSERT failure child test skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    void testInteractiveAbortChild(TestContext &context, const AssertTestOptions &options)
    {
#if GAMEWIP_ASSERT_ENABLED
        if (!options.enableChildCrashTests)
        {
            context.pass("ASSERT_INTERACTIVE abort child test disabled by AssertTestOptions");
            return;
        }

        const std::filesystem::path childLogDirectory = context.logRoot / "interactive_abort_child";
        TestSupport::createDirectories(childLogDirectory);
        const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "abort");

        expectAbnormalChildExit(context, interactiveAbortChildArgument, "ASSERT_INTERACTIVE abort child exits abnormally");

        const std::string childLogContents = readDirectoryFiles(childLogDirectory);
        context.expectTrue("ASSERT_INTERACTIVE abort child logs fatal", childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos, "interactive abort child fatal missing");
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("ASSERT_INTERACTIVE abort child logs message", childLogContents.find("interactive abort child") != std::string::npos, "interactive abort child message missing");
#else
        context.expectTrue("ASSERT_INTERACTIVE abort child strips message", childLogContents.find("interactive abort child") == std::string::npos, "interactive abort child message was embedded");
#endif
#else
        context.pass("ASSERT_INTERACTIVE abort child skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    void testInteractiveBreakChild(TestContext &context, const AssertTestOptions &options)
    {
#if GAMEWIP_ASSERT_ENABLED
        if (!options.enableChildCrashTests)
        {
            context.pass("ASSERT_INTERACTIVE break child test disabled by AssertTestOptions");
            return;
        }

        const std::filesystem::path childLogDirectory = context.logRoot / "interactive_break_child";
        TestSupport::createDirectories(childLogDirectory);
        const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "break");

        expectAbnormalChildExit(context, interactiveBreakChildArgument, "ASSERT_INTERACTIVE break child exits abnormally without debugger");

        const std::string childLogContents = readDirectoryFiles(childLogDirectory);
        context.expectTrue("ASSERT_INTERACTIVE break child logs fatal", childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos, "interactive break child fatal missing");
#if GAMEWIP_ASSERT_DIAGNOSTICS
        context.expectTrue("ASSERT_INTERACTIVE break child logs message", childLogContents.find("interactive break child") != std::string::npos, "interactive break child message missing");
#else
        context.expectTrue("ASSERT_INTERACTIVE break child strips message", childLogContents.find("interactive break child") == std::string::npos, "interactive break child message was embedded");
#endif
#else
        context.pass("ASSERT_INTERACTIVE break child skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    /// @brief Verifies DEBUG_BREAK exits abnormally in an isolated child process.
    /// @param context Test context.
    void testDebugBreakChild(TestContext &context)
    {
        expectAbnormalChildExit(context, debugBreakChildArgument, "DEBUG_BREAK child exits abnormally");
    }

    /// @brief Verifies UNREACHABLE exits abnormally unless disabled unreachable is configured as an optimizer assumption.
    /// @param context Test context.
    void testUnreachableChild(TestContext &context)
    {
#if GAMEWIP_ASSERT_ENABLED || !GAMEWIP_ASSERT_UNREACHABLE_ASSUME
        const ScopedEnvironmentVariable suppressPopupOverride(suppressPopupEnvironmentVariable, "1");
        expectAbnormalChildExit(context, unreachableChildArgument, "UNREACHABLE child exits abnormally with popup suppressed");
#else
        context.pass("UNREACHABLE child test skipped because GAMEWIP_ASSERT_UNREACHABLE_ASSUME=1");
#endif
    }

    void manualInteractiveIgnoreOnceSite()
    {
        ASSERT_INTERACTIVE_MSG(false, "manual assert UI Ignore Once test - click Ignore Once to continue");
    }

    void manualInteractiveAlwaysIgnoreSite()
    {
        ASSERT_INTERACTIVE_MSG(false, "manual assert UI Always Ignore test - click Always Ignore to suppress the second call");
    }

    void manualInteractiveBreakSite()
    {
        ASSERT_INTERACTIVE_MSG(false, "manual assert UI Break test - click Break while a debugger is attached, then continue execution");
    }

    void testManualAssertUi(TestContext &context, const AssertTestOptions &options)
    {
        if (!options.enableManualUiTests)
        {
            context.pass("manual assert UI tests skipped by AssertTestOptions");
            return;
        }

#if GAMEWIP_ASSERT_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "manual_assert_ui"))
        {
            context.fail("manual assert UI logger init", "Logger::init failed");
            return;
        }

        const ScopedClearedEnvironmentVariable clearTestAction(testActionEnvironmentVariable);
        const ScopedClearedEnvironmentVariable clearSuppressPopup(suppressPopupEnvironmentVariable);

        context.emit("[MANUAL] Assert UI Ignore Once: click Ignore Once. The test should continue.\n");
        manualInteractiveIgnoreOnceSite();
        context.pass("manual assert UI Ignore Once continued");

        context.emit("[MANUAL] Assert UI Always Ignore: click Always Ignore. The second call should not show a dialog.\n");
        manualInteractiveAlwaysIgnoreSite();
        manualInteractiveAlwaysIgnoreSite();
        context.pass("manual assert UI Always Ignore suppressed second call");

#if defined(_WIN32)
        if (IsDebuggerPresent() != FALSE)
        {
            context.emit("[MANUAL] Assert UI Break: click Break, let the debugger stop, then continue execution.\n");
            manualInteractiveBreakSite();
            context.pass("manual assert UI Break continued after debugger resume");
        }
        else
        {
            context.pass("manual assert UI Break skipped because no debugger is attached");
        }
#else
        context.pass("manual assert UI Break skipped because this manual check is Windows-only");
#endif

        if (options.enableChildCrashTests)
        {
            const std::filesystem::path childLogDirectory = context.logRoot / "manual_interactive_abort_child";
            TestSupport::createDirectories(childLogDirectory);
            const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
            const ScopedClearedEnvironmentVariable clearChildTestAction(testActionEnvironmentVariable);
            const ScopedClearedEnvironmentVariable clearChildSuppressPopup(suppressPopupEnvironmentVariable);

            context.emit("[MANUAL] Assert UI Abort: a child process dialog should appear. Click Abort; the parent should detect abnormal exit.\n");
            expectAbnormalChildExit(context, interactiveAbortChildArgument, "manual assert UI Abort child exits abnormally");

            const std::string childLogContents = readDirectoryFiles(childLogDirectory);
            context.expectTrue("manual assert UI Abort child logs fatal", childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos, "manual abort child fatal missing");
            context.expectTrue("manual assert UI Abort child logs message", childLogContents.find("interactive abort child") != std::string::npos, "manual abort child message missing");
        }
        else
        {
            context.pass("manual assert UI Abort child skipped because child crash tests are disabled");
        }

        Logger::flush(2s);
#else
        context.pass("manual assert UI tests skipped because GAMEWIP_ASSERT_ENABLED=0");
#endif
    }

    /// @brief Prints one passing-path performance metric.
    /// @param context Test context.
    /// @param name Scenario name.
    /// @param iterations Number of measured iterations.
    /// @param milliseconds Elapsed producer-side time.
    void printMetric(TestContext &context, std::string_view name, std::size_t iterations, double milliseconds)
    {
        const double nanosecondsPerCall = iterations == 0 ? 0.0 : (milliseconds * 1'000'000.0) / static_cast<double>(iterations);
        ++context.performanceScenarioCount;
        context.performanceMilliseconds += milliseconds;
        context.emit(std::format(
            "[METRIC] {} iterations={} producerMs={:.3f} nsPerCall={:.2f}\n",
            name,
            iterations,
            milliseconds,
            nanosecondsPerCall));
    }

    /// @brief Runs a timed macro scenario and keeps a tiny sink to discourage full loop removal.
    /// @param context Test context.
    /// @param name Scenario name.
    /// @param iterations Number of loop iterations.
    /// @param scenario Work to time.
    template <typename Scenario>
    void measureScenario(TestContext &context, std::string_view name, std::size_t iterations, Scenario &&scenario)
    {
        const auto start = Clock::now();
        for (std::size_t index = 0; index < iterations; ++index)
        {
            scenario(index);
        }
        const auto end = Clock::now();
        const double milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
        printMetric(context, name, iterations, milliseconds);
    }

    /// @brief Runs lightweight passing-path performance metrics for public assert macros.
    /// @param context Test context.
    /// @param options Assert test options.
    void runPerformanceMetrics(TestContext &context, const AssertTestOptions &options)
    {
        if (!options.enablePerformanceMetrics)
        {
            context.pass("assert performance metrics disabled by AssertTestOptions");
            return;
        }

        const std::size_t iterations = options.performanceIterations;
        std::size_t localSink = 0;

        measureScenario(context, "ASSERT passing", iterations, [&](std::size_t index)
                        {
                            ASSERT(index < iterations);
                            localSink += index & 1u; });

        measureScenario(context, "CHECK passing", iterations, [&](std::size_t index)
                        {
                            CHECK(index < iterations);
                            localSink += index & 1u; });

        measureScenario(context, "VERIFY passing", iterations, [&](std::size_t index)
                        {
                            VERIFY(index < iterations);
                            localSink += index & 1u; });

        measureScenario(context, "ASSERT_INTERACTIVE passing", iterations, [&](std::size_t index)
                        {
                            ASSERT_INTERACTIVE(index < iterations);
                            localSink += index & 1u; });

        measureScenario(context, "VERIFY_INTERACTIVE passing", iterations, [&](std::size_t index)
                        {
                            VERIFY_INTERACTIVE(index < iterations);
                            localSink += index & 1u; });

        measureScenario(context, "ENSURE passing", iterations, [&](std::size_t index)
                        {
                            if (ENSURE(index < iterations))
                            {
                                localSink += index & 1u;
                            } });

        performanceSink += localSink;
        context.pass("assert performance metrics completed");
    }

    /// @brief Prints the assert test summary.
    /// @param context Test context.
    /// @param milliseconds Total suite duration.
    void printSummary(TestContext &context, double milliseconds)
    {
        if (context.performanceScenarioCount > 0)
        {
            context.emit(std::format(
                "[SUMMARY] assertPerformance scenarios={} producerMs={:.3f} sink={}\n",
                context.performanceScenarioCount,
                context.performanceMilliseconds,
                performanceSink));
        }
        context.emit(std::format("[SUMMARY] Assert tests completed in {:.3f} ms\n", milliseconds));
        context.emit(std::format("[RESULT] assert passed={} failed={} skipped={}\n", context.result().passed, context.result().failed, context.result().skipped));
    }
}

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

        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("Assert test report: {}", options.writeReport ? options.reportPath.string() : std::string{"disabled"}));

        const TestSupport::Types::SuiteResult suite = runner.runSuite(
            "Assert",
            [&](TestSupport::Context &suiteContext)
            {
                TestSupport::Timer suiteTimer;
                TestContext context(suiteContext);
                context.executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : "";
                context.logRoot = makeRunRoot();
                TestSupport::createDirectories(context.logRoot);
                const ScopedLogRootCleanup cleanupLogRoot(context.logRoot);

                context.emit(std::format("[INFO] Assert test log root: {}\n", pathText(context.logRoot)));
                context.emit(std::format(
                    "[INFO] Assert config: runtime={} enabled={} checks={} diagnostics={} popupAssert={} popupCheck={}\n",
                    GAMEWIP_ASSERT_RUNTIME,
                    GAMEWIP_ASSERT_ENABLED,
                    GAMEWIP_ASSERT_CHECKS_ENABLED,
                    GAMEWIP_ASSERT_DIAGNOSTICS,
                    GAMEWIP_ASSERT_POPUP_ON_ASSERT,
                    GAMEWIP_ASSERT_POPUP_ON_CHECK));
                context.emit(std::format(
                    "[INFO] Assert test options: stress={} fatalChild={} performance={} automatedInteractive={} manualUi={} perfIterations={} stressThreads={} stressIterations={} report={}\n",
                    options.enableStressTests,
                    options.enableChildCrashTests,
                    options.enablePerformanceMetrics,
                    options.enableAutomatedInteractiveTests,
                    options.enableManualUiTests,
                    options.performanceIterations,
                    options.stressThreadCount,
                    options.stressIterations,
                    options.writeReport ? options.reportPath.string() : std::string{"disabled"}));

                runCase(context, "passing macros", [&]
                        { testPassingMacros(context); });
                runCase(context, "disabled macro evaluation", [&]
                        { testDisabledMacroEvaluation(context); });
                runCase(context, "VERIFY evaluation", [&]
                        { testVerifyEvaluation(context); });
                runCase(context, "ENSURE behavior", [&]
                        { testEnsureBehavior(context); });
                runCase(context, "CHECK_ONCE logging", [&]
                        { testCheckOnceLogging(context); });
                runCase(context, "diagnostic configuration", [&]
                        { testDiagnosticConfiguration(context); });
                runCase(context, "diagnostic message evaluation", [&]
                        { testDiagnosticMessageEvaluation(context); });
                runCase(context, "compiled-out message evaluation", [&]
                        { testCompiledOutMessageEvaluation(context); });
                runCase(context, "assert test hooks", [&]
                        { testAssertTestHooks(context); });
                runCase(context, "automated interactive assert tests", [&]
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
                    } });
                runCase(context, "CHECK_ONCE thread stress", [&]
                        { testCheckOnceThreadStress(context, options); });
                runCase(context, "ASSERT failure child", [&]
                        { testAssertFailureChild(context, options); });
                runCase(context, "DEBUG_BREAK child", [&]
                        { testDebugBreakChild(context); });
                runCase(context, "UNREACHABLE child", [&]
                        { testUnreachableChild(context); });
                runCase(context, "manual assert UI", [&]
                        { testManualAssertUi(context, options); });
                runCase(context, "performance metrics", [&]
                        { runPerformanceMetrics(context, options); });
                Logger::shutdown();
                printSummary(context, suiteTimer.elapsedMilliseconds());
            });

        runner.summary(std::format(
            "assert passed={} failed={} skipped={} elapsedMs={:.3f}",
            suite.summary.passed,
            suite.summary.failed,
            suite.summary.skipped,
            suite.elapsedMilliseconds));
        Logger::shutdown();
        return runner.exitCode();
    }

}
