#include "test/assert_test.h"

#include "debug/assert/assert.h"
#include "logger/logger.h"

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
    using AssertTestOptions = GameWIP::Test::AssertTestOptions;
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    constexpr std::string_view assertFailureChildArgument = "--assert-test-child=assert-failure";
    constexpr std::string_view debugBreakChildArgument = "--assert-test-child=debug-break";
    constexpr std::string_view unreachableChildArgument = "--assert-test-child=unreachable";
    constexpr std::string_view suppressPopupEnvironmentVariable = "GAMEWIP_ASSERT_SUPPRESS_POPUP";
    constexpr std::string_view childLogDirectoryEnvironmentVariable = "GAMEWIP_ASSERT_TEST_CHILD_LOG_DIR";
    constexpr std::string_view assertFailureChildMessage = "assert child logger message";

    std::size_t performanceSink = 0;

    /// @brief Mutable test accounting and shared paths for the assert suite.
    struct TestContext
    {
        int passed = 0;
        int failed = 0;
        std::filesystem::path logRoot;
        std::string executablePath;
        double performanceMilliseconds = 0.0;
        std::size_t performanceScenarioCount = 0;

        /// @brief Records a passed assertion-test scenario.
        /// @param name Scenario name.
        void pass(std::string_view name)
        {
            ++passed;
            std::cout << std::format("[PASS] {}\n", name);
        }

        /// @brief Records a failed assertion-test scenario.
        /// @param name Scenario name.
        /// @param details Failure details.
        void fail(std::string_view name, std::string_view details)
        {
            ++failed;
            std::cout << std::format("[FAIL] {}: {}\n", name, details);
        }

        /// @brief Expects a boolean value to be true.
        /// @param name Scenario name.
        /// @param value Value to inspect.
        /// @param details Failure details.
        void expectTrue(std::string_view name, bool value, std::string_view details = "expected true")
        {
            if (value)
            {
                pass(name);
                return;
            }
            fail(name, details);
        }

        /// @brief Expects two values to compare equal.
        /// @param name Scenario name.
        /// @param actual Actual value.
        /// @param expected Expected value.
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
    };

    /// @brief Sets a process environment variable inherited by child tests.
    /// @param name Environment variable name.
    /// @param value Environment variable value.
    void setEnvironmentVariable(std::string_view name, std::string_view value)
    {
#if defined(_WIN32)
        const std::string nameText(name);
        const std::string valueText(value);
        SetEnvironmentVariableA(nameText.c_str(), valueText.c_str());
#else
        const std::string nameText(name);
        const std::string valueText(value);
        setenv(nameText.c_str(), valueText.c_str(), 1);
#endif
    }

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

    /// @brief Quotes a command-line argument for child-process system calls.
    /// @param text Argument text.
    /// @return Quoted argument text.
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

    /// @brief Runs a child process with stdout/stderr suppressed.
    /// @param executablePath Executable to launch.
    /// @param argument Single child-mode argument to pass.
    /// @return Child process exit code, or zero if launch failed.
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
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return {};
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
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
        std::filesystem::create_directories(directory);
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
        const int result = runChildProcess(context.executablePath, argument);
        context.expectTrue(testName, result != 0, "child process returned zero");
    }

    /// @brief Verifies a failed ASSERT triggers an abnormal child-process exit when assertions are enabled.
    /// @param context Test context.
    /// @param options Assert test options.
    void testAssertFailureChild(TestContext &context, const AssertTestOptions &options)
    {
#if GAMEWIP_ASSERT_ENABLED
        if (!options.enableAssertFailureChildTest)
        {
            context.pass("ASSERT failure child test disabled by AssertTestOptions");
            return;
        }

        const std::filesystem::path childLogDirectory = context.logRoot / "assert_child";
        std::filesystem::create_directories(childLogDirectory);
        setEnvironmentVariable(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));

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
        expectAbnormalChildExit(context, unreachableChildArgument, "UNREACHABLE child exits abnormally");
#else
        context.pass("UNREACHABLE child test skipped because GAMEWIP_ASSERT_UNREACHABLE_ASSUME=1");
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
        std::cout << std::format(
            "[METRIC] {} iterations={} producerMs={:.3f} nsPerCall={:.2f}\n",
            name,
            iterations,
            milliseconds,
            nanosecondsPerCall);
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
    void printSummary(const TestContext &context, double milliseconds)
    {
        if (context.performanceScenarioCount > 0)
        {
            std::cout << std::format(
                "[SUMMARY] assertPerformance scenarios={} producerMs={:.3f} sink={}\n",
                context.performanceScenarioCount,
                context.performanceMilliseconds,
                performanceSink);
        }
        std::cout << std::format("[SUMMARY] Assert tests completed in {:.3f} ms\n", milliseconds);
        std::cout << std::format("[RESULT] assert passed={} failed={}\n", context.passed, context.failed);
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
        if (hasArgument(argc, argv, debugBreakChildArgument))
        {
            return runDebugBreakChild();
        }
        if (hasArgument(argc, argv, unreachableChildArgument))
        {
            return runUnreachableChild();
        }

        const auto suiteStart = Clock::now();
        TestContext context;
        context.executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : "";
        context.logRoot = makeRunRoot();
        std::filesystem::create_directories(context.logRoot);

        std::cout << std::format("[INFO] Assert test log root: {}\n", pathText(context.logRoot));
        std::cout << std::format(
            "[INFO] Assert config: runtime={} enabled={} checks={} diagnostics={} popupAssert={} popupCheck={}\n",
            GAMEWIP_ASSERT_RUNTIME,
            GAMEWIP_ASSERT_ENABLED,
            GAMEWIP_ASSERT_CHECKS_ENABLED,
            GAMEWIP_ASSERT_DIAGNOSTICS,
            GAMEWIP_ASSERT_POPUP_ON_ASSERT,
            GAMEWIP_ASSERT_POPUP_ON_CHECK);
        setEnvironmentVariable(suppressPopupEnvironmentVariable, "1");

        try
        {
            testPassingMacros(context);
            testDisabledMacroEvaluation(context);
            testVerifyEvaluation(context);
            testEnsureBehavior(context);
            testCheckOnceLogging(context);
            testDiagnosticConfiguration(context);
            testDiagnosticMessageEvaluation(context);
            testAssertFailureChild(context, options);
            testDebugBreakChild(context);
            testUnreachableChild(context);
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
        printSummary(context, suiteMilliseconds);
        return context.failed == 0 ? 0 : 1;
    }
}
