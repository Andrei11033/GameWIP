/// @file test_support_test.cpp
/// @brief Executable self-tests for the TestSupport library.

#include "test/test_support_test.h"

#include "test_support/test_support.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
    namespace TestSupport = GameWIP::TestSupport;
    using TestSupportTestOptions = GameWIP::Test::TestSupportTestOptions;
    using namespace std::chrono_literals;

    constexpr std::string_view kEnvironmentChildArgument = "--test-support-test-child=environment";
    constexpr std::string_view kEchoChildArgument = "--test-support-test-child=echo";
    constexpr std::string_view kSleepChildArgument = "--test-support-test-child=sleep";
    constexpr std::string_view kExitCodeChildArgument = "--test-support-test-child=exit-code";
    constexpr std::string_view kOutputChildArgument = "--test-support-test-child=output";
    constexpr std::string_view kDescendantChildArgument = "--test-support-test-child=descendant";
    constexpr std::string_view kChildSetVariable = "INTERNAL_TEST_SUPPORT_CHILD_SET";
    constexpr std::string_view kChildUnsetVariable = "INTERNAL_TEST_SUPPORT_CHILD_UNSET";
    constexpr std::string_view kScopedVariable = "INTERNAL_TEST_SUPPORT_SCOPED_ENV";

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

    std::string argumentAfter(int argc, char **argv, std::string_view argument)
    {
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]) == argument && argv[index + 1] != nullptr)
            {
                return argv[index + 1];
            }
        }
        return {};
    }

    std::filesystem::path makeRunRoot()
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto threadHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        return std::filesystem::temp_directory_path() / std::format("test_support_tests_{}_{}", ticks, threadHash);
    }

    TestSupport::Types::ReportOptions quietReport(const std::filesystem::path &path)
    {
        TestSupport::Types::ReportOptions options;
        options.writeConsole = false;
        options.writeReport = true;
        options.appendReport = false;
        options.flushReportEachLine = true;
        options.reportPath = path;
        return options;
    }

    struct ScopedPromptStreams
    {
        std::istringstream input;
        std::ostringstream output;
        std::streambuf *previousInput = nullptr;
        std::streambuf *previousOutput = nullptr;

        explicit ScopedPromptStreams(std::string_view text)
            : input(std::string(text))
        {
            previousInput = std::cin.rdbuf(input.rdbuf());
            previousOutput = std::cout.rdbuf(output.rdbuf());
        }

        ~ScopedPromptStreams()
        {
            std::cin.rdbuf(previousInput);
            std::cout.rdbuf(previousOutput);
        }

        ScopedPromptStreams(const ScopedPromptStreams &) = delete;
        ScopedPromptStreams &operator=(const ScopedPromptStreams &) = delete;
    };

    TestSupport::Types::ManualAnswer promptWithInput(std::string_view input)
    {
        ScopedPromptStreams streams(input);
        return TestSupport::promptManualCheck("automated prompt check");
    }

    int runTestSupportChild(int argc, char **argv)
    {
        if (hasArgument(argc, argv, kEnvironmentChildArgument))
        {
            const char *setValue = std::getenv(std::string(kChildSetVariable).c_str());
            const char *unsetValue = std::getenv(std::string(kChildUnsetVariable).c_str());

            std::cout << "child-set=" << (setValue != nullptr ? setValue : "<unset>") << '\n';
            std::cout << "child-unset=" << (unsetValue != nullptr ? unsetValue : "<unset>") << '\n';
            return setValue != nullptr && std::string_view(setValue) == "child-value" && unsetValue == nullptr ? 0 : 9;
        }

        if (hasArgument(argc, argv, kEchoChildArgument))
        {
            std::cout << "testing child echo\n";
            std::cerr << "testing child stderr\n";
            return 0;
        }

        if (hasArgument(argc, argv, kSleepChildArgument))
        {
            std::this_thread::sleep_for(5s);
            return 0;
        }

        if (hasArgument(argc, argv, kExitCodeChildArgument))
        {
            const std::string exitCodeText = argumentAfter(argc, argv, kExitCodeChildArgument);
            return exitCodeText.empty() ? 7 : std::stoi(exitCodeText);
        }

        if (hasArgument(argc, argv, kOutputChildArgument))
        {
            const std::string byteCountText = argumentAfter(argc, argv, kOutputChildArgument);
            const std::size_t byteCount = byteCountText.empty() ? 0 : static_cast<std::size_t>(std::stoull(byteCountText));
            const std::string output(byteCount, 'x');
            std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
            return 0;
        }

        if (hasArgument(argc, argv, kDescendantChildArgument))
        {
#if defined(_WIN32)
            std::string commandLine = "\"" + std::string(argv[0]) + "\" " + std::string(kSleepChildArgument);
            STARTUPINFOA startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInfo{};
            if (CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo) ==
                FALSE)
            {
                return 11;
            }
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            std::this_thread::sleep_for(5s);
            return 0;
#else
            return 12;
#endif
        }

        return 2;
    }

    void testSummaryAndTimer(TestSupport::Context &context)
    {
        const TestSupport::Types::ReportOptions defaultOptions;
        static_cast<void>(context.expectTrue("ReportOptions default console", defaultOptions.writeConsole));
        static_cast<void>(context.expectTrue("ReportOptions default report", defaultOptions.writeReport));
        static_cast<void>(context.expectFalse("ReportOptions default append", defaultOptions.appendReport));
        static_cast<void>(context.expectFalse("ReportOptions default per-line flush", defaultOptions.flushReportEachLine));
        static_cast<void>(
            context.expectEq("ReportOptions default path", std::filesystem::path("logs/tests/latest_test_report.txt"), defaultOptions.reportPath));

        TestSupport::Types::Summary summary;
        summary.passed = 2;
        summary.failed = 0;
        summary.skipped = 1;
        static_cast<void>(context.expectEq("Summary total", std::size_t{3}, summary.total()));
        static_cast<void>(context.expectTrue("Summary ok with no failures", summary.ok()));

        summary.failed = 1;
        static_cast<void>(context.expectFalse("Summary not ok with failures", summary.ok()));

        TestSupport::Types::SuiteResult suiteResult;
        suiteResult.summary.failed = 0;
        static_cast<void>(context.expectTrue("SuiteResult ok mirrors summary", suiteResult.ok()));
        suiteResult.summary.failed = 1;
        static_cast<void>(context.expectFalse("SuiteResult failure mirrors summary", suiteResult.ok()));

        TestSupport::Timer timer;
        static_cast<void>(context.expectTrue("Timer elapsed non-negative", timer.elapsedMilliseconds() >= 0.0));
        static_cast<void>(context.expectEq("Timer zero iterations", 0.0, timer.nanosecondsPerIteration(0)));
        timer.reset();
        std::this_thread::sleep_for(1ms);
        static_cast<void>(context.expectTrue("Timer elapsed increases", timer.elapsedMilliseconds() > 0.0));

        TestSupport::Types::IterationMetric metric;
        metric.iterations = 2;
        metric.milliseconds = 1.5;
        static_cast<void>(context.expectNear("IterationMetric ns per iteration", 750000.0, metric.nanosecondsPerIteration(), 0.001));
        metric.iterations = 0;
        static_cast<void>(context.expectEq("IterationMetric zero iterations", 0.0, metric.nanosecondsPerIteration()));
    }

    void testFileHelpers(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path path = root / "files" / "sample.txt";
        TestSupport::writeTextFile(path, "alpha beta alpha beta");
        const std::filesystem::path existingDirectory = root / "files" / "existing";
        TestSupport::createDirectories(existingDirectory);
        TestSupport::createDirectories(existingDirectory);

        static_cast<void>(context.expectTrue("fileExists detects written file", TestSupport::fileExists(path)));
        static_cast<void>(context.expectFalse("fileExists missing path false", TestSupport::fileExists(root / "files" / "missing.txt")));
        static_cast<void>(
            context.expectFalse("fileContains missing file with empty text is false", TestSupport::fileContains(root / "files" / "missing.txt", "")));
        static_cast<void>(context.expectTrue("createDirectories handles existing directory", TestSupport::fileExists(existingDirectory)));
        static_cast<void>(context.expectEq("readTextFile returns contents", std::string("alpha beta alpha beta"), TestSupport::readTextFile(path)));
        static_cast<void>(context.expectTrue("fileContains finds text", TestSupport::fileContains(path, "beta")));
        static_cast<void>(context.expectFalse("fileContains rejects missing text", TestSupport::fileContains(path, "gamma")));
        static_cast<void>(context.expectEq(
            "countFileOccurrences counts non-overlapping matches",
            std::size_t{2},
            TestSupport::countFileOccurrences(path, "alpha")));
        static_cast<void>(context.expectEq("countFileOccurrences empty needle is zero", std::size_t{0}, TestSupport::countFileOccurrences(path, "")));

        TestSupport::removeIfExists(path);
        static_cast<void>(context.expectFalse("removeIfExists removes file", TestSupport::fileExists(path)));
    }

    void testContextReporting(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path reportPath = root / "context_report.txt";
        TestSupport::Context nested("NestedContext", quietReport(reportPath));
        nested.info("info line");
        nested.manual("manual line");
        nested.metric("metric line");
        nested.stress("stress line");
        nested.summary("summary line");
        nested.pass("direct pass");
        nested.skip("direct skip", "not needed");
        static_cast<void>(nested.expectTrue("expect true pass", true));
        static_cast<void>(nested.expectFalse("expect false pass", false));
        static_cast<void>(nested.expectEq("expect eq pass", 4, 4));
        static_cast<void>(nested.expectNe("expect ne pass", 4, 5));
        static_cast<void>(nested.expectNear("expect near pass", 10.0, 10.2, 0.25));
        static_cast<void>(nested.expectContains("expect contains pass", "abcdef", "bcd"));
        static_cast<void>(nested.expectContains("expect empty substring pass", "abcdef", ""));

        const std::filesystem::path filePath = root / "context_file.txt";
        TestSupport::writeTextFile(filePath, "one two one");
        static_cast<void>(nested.expectFileContains("expect file contains pass", filePath, "two"));
        static_cast<void>(nested.expectFileOccurrenceCount("expect file occurrences pass", filePath, "one", 2));

        const TestSupport::Types::Summary nestedSummary = nested.result();
        static_cast<void>(context.expectEq("Context pass count", std::size_t{10}, nestedSummary.passed));
        static_cast<void>(context.expectEq("Context skip count", std::size_t{1}, nestedSummary.skipped));
        static_cast<void>(context.expectEq("Context fail count", std::size_t{0}, nestedSummary.failed));
        static_cast<void>(context.expectTrue("Context ok", nested.ok()));
        static_cast<void>(context.expectEq("Context suite name", std::string("NestedContext"), nested.suiteName()));

        const std::string report = TestSupport::readTextFile(reportPath);
        static_cast<void>(context.expectContains("Context report includes info", report, "[INFO] [NestedContext] info line"));
        static_cast<void>(context.expectContains("Context report includes pass", report, "[PASS] [NestedContext] direct pass"));
        static_cast<void>(context.expectContains("Context report includes skip", report, "[SKIP] [NestedContext] direct skip: not needed"));
        static_cast<void>(context.expectContains("Context report includes manual", report, "[MANUAL] [NestedContext] manual line"));
        static_cast<void>(context.expectContains("Context report includes metric", report, "[METRIC] [NestedContext] metric line"));

        TestSupport::Context failing("FailingContext", quietReport(root / "failing_context_report.txt"));
        static_cast<void>(failing.expectTrue("intentional false expectation", false));
        static_cast<void>(failing.expectNear("intentional negative tolerance", 1.0, 1.0, -1.0));
        static_cast<void>(failing.expectNear("intentional outside tolerance", 1.0, 2.0, 0.1));
        static_cast<void>(failing.expectFileContains("intentional missing file", root / "missing.txt", "needle"));
        const TestSupport::Types::Summary failingSummary = failing.result();
        static_cast<void>(context.expectEq("Context records failures without throwing", std::size_t{4}, failingSummary.failed));
        static_cast<void>(context.expectFalse("Failing context not ok", failing.ok()));
        context.pass("Context continued after nested failures");
    }

    void testReportModes(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path noReportPath = root / "no_report.txt";
        TestSupport::Types::ReportOptions noReportOptions;
        noReportOptions.writeConsole = false;
        noReportOptions.writeReport = false;
        noReportOptions.reportPath = noReportPath;
        TestSupport::Context noReport("NoReport", noReportOptions);
        noReport.pass("not written");
        static_cast<void>(context.expectFalse("Report disabled writes no file", TestSupport::fileExists(noReportPath)));

        const std::filesystem::path consoleOnlyPath = root / "console_only_report.txt";
        TestSupport::Types::ReportOptions consoleOnlyOptions;
        consoleOnlyOptions.writeConsole = true;
        consoleOnlyOptions.writeReport = false;
        consoleOnlyOptions.reportPath = consoleOnlyPath;
        TestSupport::Context consoleOnly("ConsoleOnly", consoleOnlyOptions);
        consoleOnly.pass("console only line");
        static_cast<void>(context.expectFalse("Console-only report writes no file", TestSupport::fileExists(consoleOnlyPath)));

        const std::filesystem::path appendPath = root / "append_report.txt";
        TestSupport::Types::ReportOptions firstOptions = quietReport(appendPath);
        firstOptions.appendReport = false;
        TestSupport::Context first("AppendReport", firstOptions);
        first.pass("first line");

        TestSupport::Types::ReportOptions secondOptions = quietReport(appendPath);
        secondOptions.appendReport = true;
        TestSupport::Context second("AppendReport", secondOptions);
        second.pass("second line");

        const std::string appended = TestSupport::readTextFile(appendPath);
        static_cast<void>(context.expectContains("Report append preserves first line", appended, "first line"));
        static_cast<void>(context.expectContains("Report append writes second line", appended, "second line"));

        TestSupport::Types::ReportOptions truncateOptions = quietReport(appendPath);
        truncateOptions.appendReport = false;
        TestSupport::Context truncated("AppendReport", truncateOptions);
        truncated.pass("third line");

        const std::string truncatedText = TestSupport::readTextFile(appendPath);
        static_cast<void>(context.expectContains("Report truncate writes new line", truncatedText, "third line"));
        static_cast<void>(context.expectFalse("Report truncate removes old line", truncatedText.find("first line") != std::string::npos));

        const std::filesystem::path destructionFlushPath = root / "destruction_flush_report.txt";
        {
            TestSupport::Types::ReportOptions bufferedOptions;
            bufferedOptions.writeConsole = false;
            bufferedOptions.writeReport = true;
            bufferedOptions.flushReportEachLine = false;
            bufferedOptions.reportPath = destructionFlushPath;
            TestSupport::Context buffered("BufferedReport", bufferedOptions);
            buffered.pass("flushed at destruction");
        }
        static_cast<void>(context.expectContains(
            "Report sink destruction flushes buffered output",
            TestSupport::readTextFile(destructionFlushPath),
            "flushed at destruction"));
    }

    void testPromptManualCheck(TestSupport::Context &context)
    {
        static_cast<void>(context.expectTrue("promptManualCheck accepts yes", promptWithInput("yes\n") == TestSupport::Types::ManualAnswer::Yes));
        static_cast<void>(context.expectTrue("promptManualCheck accepts y", promptWithInput("y\n") == TestSupport::Types::ManualAnswer::Yes));
        static_cast<void>(context.expectTrue("promptManualCheck accepts no", promptWithInput("no\n") == TestSupport::Types::ManualAnswer::No));
        static_cast<void>(context.expectTrue("promptManualCheck accepts n", promptWithInput("n\n") == TestSupport::Types::ManualAnswer::No));
        static_cast<void>(
            context.expectTrue("promptManualCheck accepts skipped", promptWithInput("skip\n") == TestSupport::Types::ManualAnswer::Skipped));
        static_cast<void>(context.expectTrue("promptManualCheck accepts s", promptWithInput("s\n") == TestSupport::Types::ManualAnswer::Skipped));
        static_cast<void>(
            context.expectTrue("promptManualCheck retries invalid input", promptWithInput("maybe\nYes\n") == TestSupport::Types::ManualAnswer::Yes));
        static_cast<void>(
            context.expectTrue("promptManualCheck returns skipped on EOF", promptWithInput("") == TestSupport::Types::ManualAnswer::Skipped));
    }

    void recordExpectedManualAnswer(
        TestSupport::Context &context,
        std::string_view name,
        std::string_view question,
        TestSupport::Types::ManualAnswer expectedAnswer)
    {
        const TestSupport::Types::ManualAnswer answer = TestSupport::promptManualCheck(question);
        if (answer == expectedAnswer)
        {
            context.pass(name);
            return;
        }

        if (answer == TestSupport::Types::ManualAnswer::Skipped)
        {
            context.skip(name, "manual check skipped by user");
            return;
        }

        context.fail(name, "manual answer did not match the requested response");
    }

    void testManualPromptChecks(TestSupport::Context &context, const TestSupportTestOptions &options)
    {
        if (!options.enableManualTests)
        {
            context.skip("TestSupport manual prompt checks", "disabled by TestSupportTestOptions");
            return;
        }

        recordExpectedManualAnswer(
            context,
            "manual prompt accepts yes",
            "TestSupport manual check: type yes to validate ManualAnswer::Yes.",
            TestSupport::Types::ManualAnswer::Yes);
        recordExpectedManualAnswer(
            context,
            "manual prompt accepts no",
            "TestSupport manual check: type no to validate ManualAnswer::No.",
            TestSupport::Types::ManualAnswer::No);
        recordExpectedManualAnswer(
            context,
            "manual prompt accepts skipped",
            "TestSupport manual check: type skip to validate ManualAnswer::Skipped.",
            TestSupport::Types::ManualAnswer::Skipped);
    }

    void testRunnerAndSection(TestSupport::Context &context, const std::filesystem::path &root)
    {
        TestSupport::Types::ReportOptions runnerOptions = quietReport(root / "runner_report.txt");
        runnerOptions.flushReportEachLine = false;
        TestSupport::Runner runner(runnerOptions);
        const TestSupport::Types::SuiteResult first = runner.runSuite(
            "FirstSuite",
            [](TestSupport::Context &suite)
            {
                TestSupport::Section section(suite, "small section");
                suite.pass("suite pass");
                suite.skip("suite skip", "not applicable");
            });

        const TestSupport::Types::SuiteResult second = runner.runSuite("SecondSuite", [] {});

        static_cast<void>(context.expectTrue("Runner first suite ok", first.ok()));
        static_cast<void>(context.expectTrue("Runner second suite ok", second.ok()));

        const TestSupport::Types::Summary runnerSummary = runner.result();
        static_cast<void>(context.expectEq("Runner aggregate passed", std::size_t{1}, runnerSummary.passed));
        static_cast<void>(context.expectEq("Runner aggregate skipped", std::size_t{1}, runnerSummary.skipped));
        static_cast<void>(context.expectEq("Runner aggregate failed", std::size_t{0}, runnerSummary.failed));
        static_cast<void>(context.expectTrue("Runner ok", runner.ok()));
        static_cast<void>(context.expectEq("Runner exit code success", 0, runner.exitCode()));

        const std::string report = TestSupport::readTextFile(root / "runner_report.txt");
        static_cast<void>(context.expectContains("Runner report includes result", report, "[RESULT] FirstSuite passed=1 failed=0 skipped=1"));
        static_cast<void>(context.expectContains("Section reports begin", report, "begin section: small section"));
        static_cast<void>(context.expectContains("Section reports metric", report, "section small section elapsedMs="));

        TestSupport::Runner throwingRunner(quietReport(root / "throwing_runner_report.txt"));
        const TestSupport::Types::SuiteResult throwing = throwingRunner.runSuite(
            "ThrowingSuite",
            []
            {
                throw std::runtime_error("boom");
            });
        const TestSupport::Types::SuiteResult later = throwingRunner.runSuite(
            "LaterSuite",
            [](TestSupport::Context &suite)
            {
                suite.pass("later pass");
            });

        const TestSupport::Types::Summary throwingSummary = throwingRunner.result();
        static_cast<void>(context.expectFalse("Throwing suite fails", throwing.ok()));
        static_cast<void>(context.expectTrue("Later suite still runs", later.ok()));
        static_cast<void>(context.expectEq("Throwing runner records failure", std::size_t{1}, throwingSummary.failed));
        static_cast<void>(context.expectEq("Throwing runner records later pass", std::size_t{1}, throwingSummary.passed));
        static_cast<void>(context.expectEq("Throwing runner exit code failure", 1, throwingRunner.exitCode()));
    }

    void testEnvironmentHelpers(TestSupport::Context &context)
    {
        {
            TestSupport::ScopedUnsetEnvironmentVariable clean(kScopedVariable);
            static_cast<void>(
                context.expectTrue("Scoped unset clears missing variable", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));

            {
                TestSupport::ScopedEnvironmentVariable scoped(kScopedVariable, "temporary");
                const char *value = std::getenv(std::string(kScopedVariable).c_str());
                static_cast<void>(
                    context.expectTrue("ScopedEnvironmentVariable sets value", value != nullptr && std::string_view(value) == "temporary"));
            }

            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable restores missing state", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
        }

        {
            TestSupport::ScopedEnvironmentVariable existing(kScopedVariable, "old");
            {
                TestSupport::ScopedEnvironmentVariable nested(kScopedVariable, "new");
                const char *value = std::getenv(std::string(kScopedVariable).c_str());
                static_cast<void>(
                    context.expectTrue("ScopedEnvironmentVariable overrides existing value", value != nullptr && std::string_view(value) == "new"));
            }

            const char *restored = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable restores old value", restored != nullptr && std::string_view(restored) == "old"));

            {
                TestSupport::ScopedUnsetEnvironmentVariable unset(kScopedVariable);
                static_cast<void>(context.expectTrue(
                    "ScopedUnsetEnvironmentVariable clears existing value",
                    std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
            }

            const char *afterUnset = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(context.expectTrue(
                "ScopedUnsetEnvironmentVariable restores old value",
                afterUnset != nullptr && std::string_view(afterUnset) == "old"));
        }
    }

    void testChildProcesses(TestSupport::Context &context, std::string_view executablePath, const TestSupportTestOptions &options)
    {
        if (!options.enableChildProcessTests)
        {
            context.skip("TestSupport child process tests", "disabled by TestSupportTestOptions");
            return;
        }

        {
            TestSupport::ScopedEnvironmentVariable parentUnset(kChildUnsetVariable, "parent-value");

            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kEnvironmentChildArgument)};
            childOptions.environment = {
                TestSupport::Types::EnvironmentVariable{std::string(kChildSetVariable), std::string("child-value")},
                TestSupport::Types::EnvironmentVariable{std::string(kChildUnsetVariable), std::nullopt}};
            childOptions.timeout = 5s;

            const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Child process environment exits successfully", result.exitedSuccessfully()));
            static_cast<void>(context.expectContains("Child process captures stdout", result.output, "child-set=child-value"));
            static_cast<void>(context.expectContains("Child process unsets environment", result.output, "child-unset=<unset>"));
        }

        {
            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
            childOptions.captureOutput = false;
            childOptions.timeout = 5s;

            const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Child process capture can be disabled", result.exitedSuccessfully()));
            static_cast<void>(context.expectEq("Disabled capture leaves output empty", std::string(), result.output));
        }

        {
            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kEnvironmentChildArgument)};
            childOptions.environment = {
                TestSupport::Types::EnvironmentVariable{std::string(kChildSetVariable), std::string("child-value")},
                TestSupport::Types::EnvironmentVariable{std::string(kChildUnsetVariable), std::nullopt}};
            childOptions.inheritParentEnvironment = false;
            childOptions.timeout = 5s;

            const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Child process can disable parent environment inheritance", result.exitedSuccessfully()));
        }

        {
            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kExitCodeChildArgument), "7"};
            childOptions.timeout = 5s;

            const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectEq("Child process reports nonzero exit", 7, result.exitCode));
            static_cast<void>(context.expectTrue("Child process nonzero exit is failure", result.exitedWithFailure()));
        }

        {
            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kSleepChildArgument)};
            childOptions.timeout = 50ms;

            const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Child process timeout is reported", result.timedOut));
            static_cast<void>(context.expectTrue("Child process timeout is terminated by test", result.wasTerminatedByTest));
            static_cast<void>(context.expectTrue("Child process timeout is failure", result.exitedWithFailure()));
        }

        const auto runOutputChild = [&](std::size_t outputBytes, std::size_t captureLimit)
        {
            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kOutputChildArgument), std::to_string(outputBytes)};
            childOptions.maxCapturedOutputBytes = captureLimit;
            childOptions.timeout = 5s;
            return TestSupport::runChildProcess(childOptions);
        };

        {
            const TestSupport::Types::ChildProcessResult result = runOutputChild(4, 8);
            static_cast<void>(context.expectTrue("Capture below limit succeeds", result.exitedSuccessfully()));
            static_cast<void>(context.expectEq("Capture below limit retains all bytes", std::size_t{4}, result.output.size()));
            static_cast<void>(context.expectFalse("Capture below limit is not truncated", result.outputTruncated));
        }

        {
            const TestSupport::Types::ChildProcessResult result = runOutputChild(8, 8);
            static_cast<void>(context.expectEq("Capture at limit retains all bytes", std::size_t{8}, result.output.size()));
            static_cast<void>(context.expectFalse("Capture at limit is not truncated", result.outputTruncated));
        }

        {
            const TestSupport::Types::ChildProcessResult result = runOutputChild(12, 8);
            static_cast<void>(context.expectTrue("Capture above limit still succeeds", result.exitedSuccessfully()));
            static_cast<void>(context.expectEq("Capture above limit retains prefix", std::string(8, 'x'), result.output));
            static_cast<void>(context.expectTrue("Capture above limit reports truncation", result.outputTruncated));
        }

        {
            const TestSupport::Types::ChildProcessResult result = runOutputChild(12, 0);
            static_cast<void>(context.expectTrue("Zero capture limit still drains child", result.exitedSuccessfully()));
            static_cast<void>(context.expectTrue("Zero capture limit retains no bytes", result.output.empty()));
            static_cast<void>(context.expectTrue("Zero capture limit reports truncation", result.outputTruncated));
        }

        {
            TestSupport::Types::ChildProcessOptions childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kDescendantChildArgument)};
            childOptions.timeout = 50ms;
            const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Descendant process timeout is reported", result.timedOut));
            static_cast<void>(context.expectTrue("Descendant process tree is terminated", result.wasTerminatedByTest));
        }
    }

    void testStressHelpers(TestSupport::Context &context, const TestSupportTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.skip("TestSupport stress helper tests", "disabled by TestSupportTestOptions");
            return;
        }

        TestSupport::StartGate gate;
        TestSupport::StopFlag stopFlag;
        std::atomic<std::size_t> started{0};
        std::atomic<std::size_t> stopped{0};

        std::thread opener(
            [&gate]
            {
                std::this_thread::sleep_for(10ms);
                gate.open();
            });

        TestSupport::runWorkers(
            4,
            [&](std::size_t)
            {
                gate.wait();
                const std::size_t currentStarted = started.fetch_add(1, std::memory_order_relaxed) + 1;
                if (currentStarted == 4)
                {
                    stopFlag.requestStop();
                }
                while (!stopFlag.stopRequested())
                {
                    std::this_thread::yield();
                }
                stopped.fetch_add(1, std::memory_order_relaxed);
            });

        opener.join();
        static_cast<void>(context.expectEq("StartGate releases workers", std::size_t{4}, started.load(std::memory_order_relaxed)));
        static_cast<void>(context.expectTrue("StopFlag records stop request", stopFlag.stopRequested()));
        static_cast<void>(context.expectEq("runWorkers joins all workers", std::size_t{4}, stopped.load(std::memory_order_relaxed)));

        struct WorkerCopyRecorder
        {
            std::shared_ptr<std::atomic<std::size_t>> nextId;
            std::shared_ptr<std::mutex> mutex;
            std::shared_ptr<std::set<std::size_t>> ids;
            std::size_t id;

            WorkerCopyRecorder(
                std::shared_ptr<std::atomic<std::size_t>> nextIdIn,
                std::shared_ptr<std::mutex> mutexIn,
                std::shared_ptr<std::set<std::size_t>> idsIn)
                : nextId(std::move(nextIdIn))
                , mutex(std::move(mutexIn))
                , ids(std::move(idsIn))
                , id(nextId->fetch_add(1, std::memory_order_relaxed) + 1)
            {
            }

            WorkerCopyRecorder(const WorkerCopyRecorder &other)
                : nextId(other.nextId)
                , mutex(other.mutex)
                , ids(other.ids)
                , id(nextId->fetch_add(1, std::memory_order_relaxed) + 1)
            {
            }

            WorkerCopyRecorder(WorkerCopyRecorder &&) noexcept = default;
            WorkerCopyRecorder &operator=(const WorkerCopyRecorder &) = delete;
            WorkerCopyRecorder &operator=(WorkerCopyRecorder &&) noexcept = delete;

            void operator()(std::size_t)
            {
                std::lock_guard lock(*mutex);
                ids->insert(id);
            }
        };

        auto nextWorkerId = std::make_shared<std::atomic<std::size_t>>(0);
        auto addressMutex = std::make_shared<std::mutex>();
        auto workerIds = std::make_shared<std::set<std::size_t>>();
        TestSupport::runWorkers(4, WorkerCopyRecorder{nextWorkerId, addressMutex, workerIds});
        static_cast<void>(context.expectEq("runWorkers gives each worker its own callable copy", std::size_t{4}, workerIds->size()));

        std::atomic<std::size_t> enteredFailureWorkers{0};
        std::atomic<std::size_t> completedFailureWorkers{0};
        bool rethrown = false;
        try
        {
            TestSupport::runWorkers(
                4,
                [&](std::size_t workerIndex)
                {
                    enteredFailureWorkers.fetch_add(1, std::memory_order_relaxed);
                    if (workerIndex == 2)
                    {
                        throw std::runtime_error("intentional worker failure");
                    }
                    completedFailureWorkers.fetch_add(1, std::memory_order_relaxed);
                });
        }
        catch (const std::runtime_error &)
        {
            rethrown = true;
        }

        static_cast<void>(context.expectTrue("runWorkers rethrows worker failure", rethrown));
        static_cast<void>(
            context.expectEq("runWorkers starts every failure-path worker", std::size_t{4}, enteredFailureWorkers.load(std::memory_order_relaxed)));
        static_cast<void>(context.expectEq(
            "runWorkers joins non-throwing failure-path workers",
            std::size_t{3},
            completedFailureWorkers.load(std::memory_order_relaxed)));
    }
} // namespace

namespace GameWIP::Test
{
    int runTestSupportTests(int argc, char **argv, const TestSupportTestOptions &options)
    {
        if (hasArgument(argc, argv, kEnvironmentChildArgument) || hasArgument(argc, argv, kEchoChildArgument) ||
            hasArgument(argc, argv, kSleepChildArgument) || hasArgument(argc, argv, kExitCodeChildArgument) ||
            hasArgument(argc, argv, kOutputChildArgument) || hasArgument(argc, argv, kDescendantChildArgument))
        {
            return runTestSupportChild(argc, argv);
        }

        const std::filesystem::path runRoot = makeRunRoot();
        TestSupport::createDirectories(runRoot);

        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("TestSupport library test root: {}", runRoot.string()));
        runner.info(
            std::format(
                "TestSupport test options: childProcess={} stress={} report={}",
                options.enableChildProcessTests,
                options.enableStressTests,
                options.writeReport ? options.reportPath.string() : std::string_view{"disabled"}));

        const std::string executablePath = argc > 0 && argv[0] != nullptr ? argv[0] : "";

        runner.runSuite("TestSupport core", testSummaryAndTimer);
        runner.runSuite(
            "TestSupport files",
            [&runRoot](TestSupport::Context &context)
            {
                testFileHelpers(context, runRoot);
            });
        runner.runSuite(
            "TestSupport context",
            [&runRoot](TestSupport::Context &context)
            {
                testContextReporting(context, runRoot);
            });
        runner.runSuite(
            "TestSupport reports",
            [&runRoot](TestSupport::Context &context)
            {
                testReportModes(context, runRoot);
            });
        runner.runSuite("TestSupport prompt", testPromptManualCheck);
        runner.runSuite(
            "TestSupport manual checks",
            [&options](TestSupport::Context &context)
            {
                testManualPromptChecks(context, options);
            });
        runner.runSuite(
            "TestSupport runner",
            [&runRoot](TestSupport::Context &context)
            {
                testRunnerAndSection(context, runRoot);
            });
        runner.runSuite("TestSupport environment", testEnvironmentHelpers);
        runner.runSuite(
            "TestSupport child processes",
            [&executablePath, &options](TestSupport::Context &context)
            {
                testChildProcesses(context, executablePath, options);
            });
        runner.runSuite(
            "TestSupport stress helpers",
            [&options](TestSupport::Context &context)
            {
                testStressHelpers(context, options);
            });

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("TestSupport library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        TestSupport::removeIfExists(runRoot);
        return runner.exitCode();
    }
} // namespace GameWIP::Test
