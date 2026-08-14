/// @file test_support_test.cpp
/// @brief Executable self-tests for the TestSupport library.
///
/// Coverage includes process-global guards, report sinks, child-process cleanup,
/// manual prompts, temporary resources, and deterministic stress primitives.

#include "validation/tests/test_support/test_support_test.h"

#include "test_support/test_support.h"

#ifndef TEST_SUPPORT_INTERNAL_TEST_HOOKS
#define TEST_SUPPORT_INTERNAL_TEST_HOOKS 0
#endif

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
#include "test_support/internal/test_support_test_hooks.h"
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
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
    constexpr std::string_view kHandleInheritanceChildArgument = "--test-support-test-child=handle-inheritance";
    constexpr std::string_view kChildSetVariable = "INTERNAL_TEST_SUPPORT_CHILD_SET";
    constexpr std::string_view kChildUnsetVariable = "INTERNAL_TEST_SUPPORT_CHILD_UNSET";
    constexpr std::string_view kScopedVariable = "INTERNAL_TEST_SUPPORT_SCOPED_ENV";

    /// @brief Returns whether one exact child-mode argument is present.
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

    /// @brief Returns the argument immediately following a named option.
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

    /// @brief Creates file-only report options for tests that inspect emitted text.
    TestSupport::Types::Reporting::Options quietReport(const std::filesystem::path &path)
    {
        TestSupport::Types::Reporting::Options options;
        options.writeConsole = false;
        options.writeReport = true;
        options.appendReport = false;
        options.flushReportEachLine = true;
        options.reportPath = path;
        return options;
    }

    /// @brief Redirects standard input/output while a prompt test owns the process streams.
    struct ScopedPromptStreams
    {
        std::istringstream input;
        std::ostringstream output;
        std::streambuf *previousInput = nullptr;
        std::streambuf *previousOutput = nullptr;

        /// @brief Replaces standard prompt streams with deterministic string streams.
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

    /// @brief Runs one manual prompt against deterministic captured input.
    TestSupport::Types::Reporting::ManualAnswer promptWithInput(std::string_view input)
    {
        ScopedPromptStreams streams(input);
        return TestSupport::promptManualCheck("automated prompt check");
    }

    /// @brief Implements child modes used to verify capture, timeout, environment, and exit behavior.
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

        if (hasArgument(argc, argv, kHandleInheritanceChildArgument))
        {
#if defined(_WIN32)
            const std::string handleText = argumentAfter(argc, argv, kHandleInheritanceChildArgument);
            const auto handleValue = static_cast<std::uintptr_t>(std::stoull(handleText));
            static_cast<void>(SetEvent(reinterpret_cast<HANDLE>(handleValue)));
#endif
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

    /// @brief Verifies summary arithmetic, result predicates, defaults, and timer units.
    void testSummaryAndTimer(TestSupport::Context &context)
    {
        const TestSupport::Types::Reporting::Options defaultOptions;
        static_cast<void>(context.expectTrue("ReportOptions default console", defaultOptions.writeConsole));
        static_cast<void>(context.expectTrue("ReportOptions default report", defaultOptions.writeReport));
        static_cast<void>(context.expectFalse("ReportOptions default append", defaultOptions.appendReport));
        static_cast<void>(context.expectFalse("ReportOptions default per-line flush", defaultOptions.flushReportEachLine));
        static_cast<void>(context.expectEq(
            "ReportOptions default console verbosity",
            TestSupport::Types::Reporting::ConsoleVerbosity::Full,
            defaultOptions.consoleVerbosity));
        static_cast<void>(
            context.expectEq("ReportOptions default path", std::filesystem::path("logs/tests/latest_test_report.txt"), defaultOptions.reportPath));

        TestSupport::Types::Reporting::Summary summary;
        summary.passed = 2;
        summary.failed = 0;
        summary.skipped = 1;
        static_cast<void>(context.expectEq("Summary total", std::size_t{3}, summary.total()));
        static_cast<void>(context.expectTrue("Summary ok with no failures", summary.ok()));

        summary.failed = 1;
        static_cast<void>(context.expectFalse("Summary not ok with failures", summary.ok()));

        TestSupport::Types::Reporting::SuiteResult suiteResult;
        suiteResult.summary.failed = 0;
        static_cast<void>(context.expectTrue("SuiteResult ok mirrors summary", suiteResult.ok()));
        suiteResult.summary.failed = 1;
        static_cast<void>(context.expectFalse("SuiteResult failure mirrors summary", suiteResult.ok()));

        static_cast<void>(
            context.expectEq("Infrastructure success has a stable diagnostic", std::string("None"), TestSupport::formatInfrastructureStatus({})));
        static_cast<void>(context.expectEq(
            "Infrastructure failure diagnostic preserves category and native code",
            std::string("FileOperationFailed (nativeCode=42)"),
            TestSupport::formatInfrastructureStatus({.error = TestSupport::Types::InfrastructureError::FileOperationFailed, .nativeCode = 42})));

        TestSupport::Timer timer;
        static_cast<void>(context.expectTrue("Timer elapsed non-negative", timer.elapsedMilliseconds() >= 0.0));
        timer.reset();
        std::this_thread::sleep_for(1ms);
        static_cast<void>(context.expectTrue("Timer elapsed increases", timer.elapsedMilliseconds() > 0.0));
    }

    /// @brief Verifies text helpers and scoped temporary/current-directory lifetime behavior.
    void testFileHelpers(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path path = root / "files" / "sample.txt";
        static_cast<void>(context.expectTrue("writeTextFile succeeds", TestSupport::writeTextFile(path, "alpha beta alpha beta").ok()));
        const std::filesystem::path existingDirectory = root / "files" / "existing";
        static_cast<void>(context.expectTrue("createDirectories succeeds", TestSupport::createDirectories(existingDirectory).ok()));
        static_cast<void>(context.expectTrue("createDirectories handles existing directory", TestSupport::createDirectories(existingDirectory).ok()));

        const TestSupport::Types::BoolResult existingFile = TestSupport::fileExists(path);
        static_cast<void>(context.expectTrue("fileExists written-file inspection succeeds", existingFile.status.ok()));
        static_cast<void>(context.expectTrue("fileExists detects written file", existingFile.value));

        const std::filesystem::path missingPath = root / "files" / "missing.txt";
        const TestSupport::Types::BoolResult missingFile = TestSupport::fileExists(missingPath);
        static_cast<void>(context.expectTrue("fileExists missing-path inspection succeeds", missingFile.status.ok()));
        static_cast<void>(context.expectFalse("fileExists missing path false", missingFile.value));

        const TestSupport::Types::BoolResult missingContains = TestSupport::fileContains(missingPath, "");
        static_cast<void>(context.expectFalse("fileContains missing file reports failed status", missingContains.status.ok()));
        static_cast<void>(context.expectFalse("fileContains missing file has no match", missingContains.value));

        const TestSupport::Types::BoolResult existingDirectoryResult = TestSupport::fileExists(existingDirectory);
        static_cast<void>(context.expectTrue(
            "createDirectories produces an inspectable directory",
            existingDirectoryResult.status.ok() && existingDirectoryResult.value));

        const TestSupport::Types::TextResult readResult = TestSupport::readTextFile(path);
        static_cast<void>(context.expectTrue("readTextFile succeeds", readResult.status.ok()));
        static_cast<void>(context.expectEq("readTextFile returns contents", std::string("alpha beta alpha beta"), readResult.text));

        const TestSupport::Types::BoolResult containsBeta = TestSupport::fileContains(path, "beta");
        static_cast<void>(context.expectTrue("fileContains read succeeds", containsBeta.status.ok()));
        static_cast<void>(context.expectTrue("fileContains finds text", containsBeta.value));
        const TestSupport::Types::BoolResult containsGamma = TestSupport::fileContains(path, "gamma");
        static_cast<void>(context.expectTrue("fileContains missing-text read succeeds", containsGamma.status.ok()));
        static_cast<void>(context.expectFalse("fileContains rejects missing text", containsGamma.value));

        const TestSupport::Types::CountResult alphaCount = TestSupport::countFileOccurrences(path, "alpha");
        static_cast<void>(context.expectTrue("countFileOccurrences read succeeds", alphaCount.status.ok()));
        static_cast<void>(context.expectEq("countFileOccurrences counts non-overlapping matches", std::size_t{2}, alphaCount.count));
        const TestSupport::Types::CountResult emptyCount = TestSupport::countFileOccurrences(path, "");
        static_cast<void>(context.expectTrue("countFileOccurrences empty-needle read succeeds", emptyCount.status.ok()));
        static_cast<void>(context.expectEq("countFileOccurrences empty needle is zero", std::size_t{0}, emptyCount.count));

        static_cast<void>(context.expectTrue("removeIfExists succeeds", TestSupport::removeIfExists(path).ok()));
        const TestSupport::Types::BoolResult removedFile = TestSupport::fileExists(path);
        static_cast<void>(context.expectTrue("removed-file inspection succeeds", removedFile.status.ok()));
        static_cast<void>(context.expectFalse("removeIfExists removes file", removedFile.value));

        std::filesystem::path firstTemporaryPath;
        std::filesystem::path secondTemporaryPath;
        {
            const TestSupport::ScopedTemporaryDirectory first("scoped directory");
            const TestSupport::ScopedTemporaryDirectory second("scoped directory");
            static_cast<void>(context.expectTrue("First temporary-directory construction succeeds", first.status().ok()));
            static_cast<void>(context.expectTrue("Second temporary-directory construction succeeds", second.status().ok()));
            firstTemporaryPath = first.path();
            secondTemporaryPath = second.path();
            static_cast<void>(context.expectTrue(
                "ScopedTemporaryDirectory nested fixture write succeeds",
                TestSupport::writeTextFile(firstTemporaryPath / "nested" / "artifact.txt", "temporary").ok()));

            const TestSupport::Types::BoolResult firstExists = TestSupport::fileExists(firstTemporaryPath);
            static_cast<void>(context.expectTrue("ScopedTemporaryDirectory creates its directory", firstExists.status.ok() && firstExists.value));
            const TestSupport::Types::BoolResult artifactExists = TestSupport::fileExists(firstTemporaryPath / "nested" / "artifact.txt");
            static_cast<void>(
                context.expectTrue("ScopedTemporaryDirectory supports nested artifacts", artifactExists.status.ok() && artifactExists.value));
            static_cast<void>(context.expectNe("ScopedTemporaryDirectory paths are unique", firstTemporaryPath, secondTemporaryPath));
        }
        const TestSupport::Types::BoolResult firstRemoved = TestSupport::fileExists(firstTemporaryPath);
        const TestSupport::Types::BoolResult secondRemoved = TestSupport::fileExists(secondTemporaryPath);
        static_cast<void>(context.expectTrue("First temporary-directory cleanup is inspectable", firstRemoved.status.ok()));
        static_cast<void>(context.expectFalse("ScopedTemporaryDirectory removes its directory tree", firstRemoved.value));
        static_cast<void>(context.expectTrue("Second temporary-directory cleanup is inspectable", secondRemoved.status.ok()));
        static_cast<void>(context.expectFalse("ScopedTemporaryDirectory removes each unique directory", secondRemoved.value));

        const std::filesystem::path originalCurrentPath = std::filesystem::current_path();
        {
            const TestSupport::ScopedCurrentPath temporaryCurrentPath(root);
            static_cast<void>(context.expectTrue("ScopedCurrentPath construction succeeds", temporaryCurrentPath.status().ok()));
            static_cast<void>(
                context.expectEq("ScopedCurrentPath stores the previous path", originalCurrentPath, temporaryCurrentPath.previousPath()));
            static_cast<void>(context.expectEq("ScopedCurrentPath changes the process path", root, std::filesystem::current_path()));
        }
        static_cast<void>(context.expectEq("ScopedCurrentPath restores the process path", originalCurrentPath, std::filesystem::current_path()));

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        {
            using FailurePoint = TestSupport::TestHooks::FileFailurePoint;
            constexpr std::uint64_t nativeCode = 0x2001u;
            TestSupport::TestHooks::reset();

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Read, nativeCode);
            const TestSupport::Types::TextResult injectedRead = TestSupport::readTextFile(path);
            static_cast<void>(context.expectEq(
                "Injected file read reports file failure",
                TestSupport::Types::InfrastructureError::FileOperationFailed,
                injectedRead.status.error));
            static_cast<void>(context.expectEq("Injected file read preserves native code", nativeCode, injectedRead.status.nativeCode));

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Write, nativeCode);
            static_cast<void>(context.expectEq(
                "Injected file write preserves native code",
                nativeCode,
                TestSupport::writeTextFile(root / "injected_write.txt", "text").nativeCode));

            constexpr std::uint64_t permissionDeniedCode = 5u;
            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Write, permissionDeniedCode);
            const TestSupport::Types::InfrastructureStatus permissionFailure = TestSupport::writeTextFile(root / "permission_denied.txt", "text");
            static_cast<void>(context.expectEq(
                "Injected permission denial reports file failure",
                TestSupport::Types::InfrastructureError::FileOperationFailed,
                permissionFailure.error));
            static_cast<void>(context.expectEq(
                "Injected permission denial preserves native access-denied code",
                permissionDeniedCode,
                permissionFailure.nativeCode));

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Exists, nativeCode);
            const TestSupport::Types::BoolResult injectedExists = TestSupport::fileExists(root);
            static_cast<void>(context.expectFalse("Injected existence failure is not absence", injectedExists.status.ok()));
            static_cast<void>(context.expectEq("Injected existence failure preserves native code", nativeCode, injectedExists.status.nativeCode));

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::CreateDirectories, nativeCode);
            static_cast<void>(context.expectEq(
                "Injected directory creation preserves native code",
                nativeCode,
                TestSupport::createDirectories(root / "injected_directory").nativeCode));

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::Remove, nativeCode);
            static_cast<void>(context.expectEq(
                "Injected removal preserves native code",
                nativeCode,
                TestSupport::removeIfExists(root / "injected_remove").nativeCode));

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::TemporaryDirectory, nativeCode);
            const TestSupport::ScopedTemporaryDirectory failedTemporaryDirectory("injected");
            static_cast<void>(context.expectEq(
                "Injected temporary-directory construction preserves native code",
                nativeCode,
                failedTemporaryDirectory.status().nativeCode));
            static_cast<void>(context.expectTrue("Failed temporary-directory guard is inert", failedTemporaryDirectory.path().empty()));

            TestSupport::TestHooks::forceNextFileFailure(FailurePoint::CurrentPath, nativeCode);
            const TestSupport::ScopedCurrentPath failedCurrentPath(root);
            static_cast<void>(
                context.expectEq("Injected current-path construction preserves native code", nativeCode, failedCurrentPath.status().nativeCode));
            static_cast<void>(context.expectTrue("Failed current-path guard is inert", failedCurrentPath.previousPath().empty()));
            static_cast<void>(
                context.expectEq("Failed current-path guard does not mutate process state", originalCurrentPath, std::filesystem::current_path()));

            TestSupport::TestHooks::reset();
        }
#endif
    }

    /// @brief Verifies Context categories, expectation diagnostics, counts, and report failures.
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
        static_cast<void>(context.expectTrue("context fixture write succeeds", TestSupport::writeTextFile(filePath, "one two one").ok()));
        static_cast<void>(nested.expectFileContains("expect file contains pass", filePath, "two"));
        static_cast<void>(nested.expectFileOccurrenceCount("expect file occurrences pass", filePath, "one", 2));

        const TestSupport::Types::Reporting::Summary nestedSummary = nested.result();
        static_cast<void>(context.expectEq("Context pass count", std::size_t{10}, nestedSummary.passed));
        static_cast<void>(context.expectEq("Context skip count", std::size_t{1}, nestedSummary.skipped));
        static_cast<void>(context.expectEq("Context fail count", std::size_t{0}, nestedSummary.failed));
        static_cast<void>(context.expectTrue("Context ok", nested.ok()));
        static_cast<void>(context.expectEq("Context suite name", std::string("NestedContext"), nested.suiteName()));

        const TestSupport::Types::TextResult reportResult = TestSupport::readTextFile(reportPath);
        static_cast<void>(context.expectTrue("Context report read succeeds", reportResult.status.ok()));
        const std::string &report = reportResult.text;
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
        const TestSupport::Types::Reporting::Summary failingSummary = failing.result();
        static_cast<void>(context.expectEq("Context records failures without throwing", std::size_t{4}, failingSummary.failed));
        static_cast<void>(context.expectFalse("Failing context not ok", failing.ok()));
        context.pass("Context continued after nested failures");
    }

    /// @brief Verifies append, truncate, flush, and console-verbosity report modes.
    void testReportModes(TestSupport::Context &context, const std::filesystem::path &root)
    {
        const std::filesystem::path noReportPath = root / "no_report.txt";
        TestSupport::Types::Reporting::Options noReportOptions;
        noReportOptions.writeConsole = false;
        noReportOptions.writeReport = false;
        noReportOptions.reportPath = noReportPath;
        TestSupport::Context noReport("NoReport", noReportOptions);
        noReport.pass("not written");
        const TestSupport::Types::BoolResult noReportExists = TestSupport::fileExists(noReportPath);
        static_cast<void>(context.expectTrue("Report-disabled path inspection succeeds", noReportExists.status.ok()));
        static_cast<void>(context.expectFalse("Report disabled writes no file", noReportExists.value));

        const std::filesystem::path consoleOnlyPath = root / "console_only_report.txt";
        TestSupport::Types::Reporting::Options consoleOnlyOptions;
        consoleOnlyOptions.writeConsole = true;
        consoleOnlyOptions.writeReport = false;
        consoleOnlyOptions.reportPath = consoleOnlyPath;
        std::string consoleOnlyOutput;
        {
            ScopedPromptStreams captured("");
            TestSupport::Context consoleOnly("ConsoleOnly", consoleOnlyOptions);
            consoleOnly.pass("console only line");
            consoleOnlyOutput = captured.output.str();
        }
        static_cast<void>(context.expectContains("Console-only report writes to stdout", consoleOnlyOutput, "console only line"));
        const TestSupport::Types::BoolResult consoleOnlyExists = TestSupport::fileExists(consoleOnlyPath);
        static_cast<void>(context.expectTrue("Console-only path inspection succeeds", consoleOnlyExists.status.ok()));
        static_cast<void>(context.expectFalse("Console-only report writes no file", consoleOnlyExists.value));

        const std::filesystem::path appendPath = root / "append_report.txt";
        TestSupport::Types::Reporting::Options firstOptions = quietReport(appendPath);
        firstOptions.appendReport = false;
        TestSupport::Context first("AppendReport", firstOptions);
        first.pass("first line");

        TestSupport::Types::Reporting::Options secondOptions = quietReport(appendPath);
        secondOptions.appendReport = true;
        TestSupport::Context second("AppendReport", secondOptions);
        second.pass("second line");

        const TestSupport::Types::TextResult appendedResult = TestSupport::readTextFile(appendPath);
        static_cast<void>(context.expectTrue("Appended report read succeeds", appendedResult.status.ok()));
        const std::string &appended = appendedResult.text;
        static_cast<void>(context.expectContains("Report append preserves first line", appended, "first line"));
        static_cast<void>(context.expectContains("Report append writes second line", appended, "second line"));

        TestSupport::Types::Reporting::Options truncateOptions = quietReport(appendPath);
        truncateOptions.appendReport = false;
        TestSupport::Context truncated("AppendReport", truncateOptions);
        truncated.pass("third line");

        const TestSupport::Types::TextResult truncatedResult = TestSupport::readTextFile(appendPath);
        static_cast<void>(context.expectTrue("Truncated report read succeeds", truncatedResult.status.ok()));
        const std::string &truncatedText = truncatedResult.text;
        static_cast<void>(context.expectContains("Report truncate writes new line", truncatedText, "third line"));
        static_cast<void>(context.expectFalse("Report truncate removes old line", truncatedText.find("first line") != std::string::npos));

        const std::filesystem::path destructionFlushPath = root / "destruction_flush_report.txt";
        {
            TestSupport::Types::Reporting::Options bufferedOptions;
            bufferedOptions.writeConsole = false;
            bufferedOptions.writeReport = true;
            bufferedOptions.flushReportEachLine = false;
            bufferedOptions.reportPath = destructionFlushPath;
            TestSupport::Context buffered("BufferedReport", bufferedOptions);
            buffered.pass("flushed at destruction");
        }
        const TestSupport::Types::TextResult destructionFlushResult = TestSupport::readTextFile(destructionFlushPath);
        static_cast<void>(context.expectTrue("Destructor-flushed report read succeeds", destructionFlushResult.status.ok()));
        static_cast<void>(
            context.expectContains("Report sink destruction flushes buffered output", destructionFlushResult.text, "flushed at destruction"));

        std::string conciseOutput;
        {
            ScopedPromptStreams captured("");
            TestSupport::Types::Reporting::Options conciseOptions;
            conciseOptions.writeReport = false;
            conciseOptions.consoleVerbosity = TestSupport::Types::Reporting::ConsoleVerbosity::Concise;
            TestSupport::Context concise("Concise", conciseOptions);
            concise.info("hidden info");
            concise.pass("hidden pass");
            concise.fail("visible failure", "expected test failure");
            concise.skip("visible skip", "expected test skip");
            concise.summary("visible summary");
            conciseOutput = captured.output.str();
        }
        static_cast<void>(context.expectFalse("Concise console hides info", conciseOutput.find("hidden info") != std::string::npos));
        static_cast<void>(context.expectFalse("Concise console hides passes", conciseOutput.find("hidden pass") != std::string::npos));
        static_cast<void>(context.expectContains("Concise console includes failures", conciseOutput, "visible failure"));
        static_cast<void>(context.expectContains("Concise console includes skips", conciseOutput, "visible skip"));
        static_cast<void>(context.expectContains("Concise console includes summaries", conciseOutput, "visible summary"));

        std::string minimalOutput;
        {
            ScopedPromptStreams captured("");
            TestSupport::Types::Reporting::Options minimalOptions;
            minimalOptions.writeReport = false;
            minimalOptions.consoleVerbosity = TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
            TestSupport::Context minimal("Minimal", minimalOptions);
            minimal.info("hidden minimal info");
            minimal.pass("hidden minimal pass");
            minimal.fail("visible minimal failure", "expected test failure");
            minimal.skip("visible minimal skip", "expected test skip");
            minimal.manual("visible minimal instruction");
            minimal.summary("hidden minimal summary");
            minimalOutput = captured.output.str();
        }
        static_cast<void>(context.expectFalse("Minimal console hides info", minimalOutput.find("hidden minimal info") != std::string::npos));
        static_cast<void>(context.expectFalse("Minimal console hides passes", minimalOutput.find("hidden minimal pass") != std::string::npos));
        static_cast<void>(context.expectFalse("Minimal console hides summaries", minimalOutput.find("hidden minimal summary") != std::string::npos));
        static_cast<void>(context.expectContains("Minimal console includes failures", minimalOutput, "visible minimal failure"));
        static_cast<void>(context.expectContains("Minimal console includes skips", minimalOutput, "visible minimal skip"));
        static_cast<void>(context.expectContains("Minimal console includes manual instructions", minimalOutput, "visible minimal instruction"));
    }

    /// @brief Verifies accepted manual-answer spellings, retries, and EOF behavior.
    void testPromptManualCheck(TestSupport::Context &context)
    {
        static_cast<void>(
            context.expectTrue("promptManualCheck accepts yes", promptWithInput("yes\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
        static_cast<void>(
            context.expectTrue("promptManualCheck accepts y", promptWithInput("y\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
        static_cast<void>(
            context.expectTrue("promptManualCheck accepts no", promptWithInput("no\n") == TestSupport::Types::Reporting::ManualAnswer::No));
        static_cast<void>(
            context.expectTrue("promptManualCheck accepts n", promptWithInput("n\n") == TestSupport::Types::Reporting::ManualAnswer::No));
        static_cast<void>(context.expectTrue(
            "promptManualCheck accepts skipped",
            promptWithInput("skip\n") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
        static_cast<void>(
            context.expectTrue("promptManualCheck accepts s", promptWithInput("s\n") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
        static_cast<void>(context.expectTrue(
            "promptManualCheck retries invalid input",
            promptWithInput("maybe\nYes\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
        static_cast<void>(context.expectTrue(
            "promptManualCheck returns skipped on EOF",
            promptWithInput("") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
    }

    /// @brief Converts one expected manual response into a pass, skip, or failure.
    void recordExpectedManualAnswer(
        TestSupport::Context &context,
        std::string_view name,
        std::string_view question,
        TestSupport::Types::Reporting::ManualAnswer expectedAnswer)
    {
        const TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::promptManualCheck(question);
        if (answer == expectedAnswer)
        {
            context.pass(name);
            return;
        }

        if (answer == TestSupport::Types::Reporting::ManualAnswer::Skipped)
        {
            context.skip(name, "manual check skipped by user");
            return;
        }

        context.fail(name, "manual answer did not match the requested response");
    }

    /// @brief Runs gated human prompt checks or records their disabled skip.
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
            TestSupport::Types::Reporting::ManualAnswer::Yes);
        recordExpectedManualAnswer(
            context,
            "manual prompt accepts no",
            "TestSupport manual check: type no to validate ManualAnswer::No.",
            TestSupport::Types::Reporting::ManualAnswer::No);
        recordExpectedManualAnswer(
            context,
            "manual prompt accepts skipped",
            "TestSupport manual check: type skip to validate ManualAnswer::Skipped.",
            TestSupport::Types::Reporting::ManualAnswer::Skipped);
    }

    /// @brief Verifies Runner aggregation, exception capture, and Section diagnostics.
    void testRunnerAndSection(TestSupport::Context &context, const std::filesystem::path &root)
    {
        TestSupport::Types::Reporting::Options runnerOptions = quietReport(root / "runner_report.txt");
        runnerOptions.flushReportEachLine = false;
        TestSupport::Runner runner(runnerOptions);
        const TestSupport::Types::Reporting::SuiteResult first = runner.runSuite(
            "FirstSuite",
            [](TestSupport::Context &suite)
            {
                TestSupport::Section section(suite, "small section");
                suite.pass("suite pass");
                suite.skip("suite skip", "not applicable");
            });

        const TestSupport::Types::Reporting::SuiteResult second = runner.runSuite("SecondSuite", [] {});

        static_cast<void>(context.expectTrue("Runner first suite ok", first.ok()));
        static_cast<void>(context.expectTrue("Runner second suite ok", second.ok()));

        const TestSupport::Types::Reporting::Summary runnerSummary = runner.result();
        static_cast<void>(context.expectEq("Runner aggregate passed", std::size_t{1}, runnerSummary.passed));
        static_cast<void>(context.expectEq("Runner aggregate skipped", std::size_t{1}, runnerSummary.skipped));
        static_cast<void>(context.expectEq("Runner aggregate failed", std::size_t{0}, runnerSummary.failed));
        static_cast<void>(context.expectTrue("Runner ok", runner.ok()));
        static_cast<void>(context.expectEq("Runner exit code success", 0, runner.exitCode()));

        const TestSupport::Types::TextResult reportResult = TestSupport::readTextFile(root / "runner_report.txt");
        static_cast<void>(context.expectTrue("Runner report read succeeds", reportResult.status.ok()));
        static_cast<void>(
            context.expectContains("Runner report includes result", reportResult.text, "[RESULT] FirstSuite passed=1 failed=0 skipped=1"));
        static_cast<void>(context.expectContains("Section reports begin", reportResult.text, "begin section: small section"));
        static_cast<void>(context.expectContains("Section reports metric", reportResult.text, "section small section elapsedMs="));

        TestSupport::Runner throwingRunner(quietReport(root / "throwing_runner_report.txt"));
        const TestSupport::Types::Reporting::SuiteResult throwing = throwingRunner.runSuite(
            "ThrowingSuite",
            []
            {
                throw std::runtime_error("boom");
            });
        const TestSupport::Types::Reporting::SuiteResult later = throwingRunner.runSuite(
            "LaterSuite",
            [](TestSupport::Context &suite)
            {
                suite.pass("later pass");
            });

        const TestSupport::Types::Reporting::Summary throwingSummary = throwingRunner.result();
        static_cast<void>(context.expectFalse("Throwing suite fails", throwing.ok()));
        static_cast<void>(context.expectTrue("Later suite still runs", later.ok()));
        static_cast<void>(context.expectEq("Throwing runner records failure", std::size_t{1}, throwingSummary.failed));
        static_cast<void>(context.expectEq("Throwing runner records later pass", std::size_t{1}, throwingSummary.passed));
        static_cast<void>(context.expectEq("Throwing runner exit code failure", 1, throwingRunner.exitCode()));
    }

    /// @brief Verifies scoped environment set/unset and exact restoration behavior.
    void testEnvironmentHelpers(TestSupport::Context &context)
    {
        const TestSupport::ScopedEnvironmentVariable emptyName("", "value");
        static_cast<void>(context.expectEq(
            "ScopedEnvironmentVariable rejects an empty name",
            TestSupport::Types::InfrastructureError::InvalidArgument,
            emptyName.status().error));

        const TestSupport::ScopedUnsetEnvironmentVariable equalsName("INVALID=NAME");
        static_cast<void>(context.expectEq(
            "ScopedUnsetEnvironmentVariable rejects '=' in a name",
            TestSupport::Types::InfrastructureError::InvalidArgument,
            equalsName.status().error));

        const TestSupport::ScopedEnvironmentVariable invalidUtf8("\xFF", "value");
        static_cast<void>(context.expectEq(
            "ScopedEnvironmentVariable rejects invalid UTF-8",
            TestSupport::Types::InfrastructureError::InvalidArgument,
            invalidUtf8.status().error));

        {
            TestSupport::ScopedUnsetEnvironmentVariable clean(kScopedVariable);
            static_cast<void>(context.expectTrue("Initial scoped unset succeeds", clean.status().ok()));
            static_cast<void>(
                context.expectTrue("Scoped unset clears missing variable", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));

            {
                TestSupport::ScopedEnvironmentVariable scoped(kScopedVariable, "temporary");
                static_cast<void>(context.expectTrue("Scoped environment set succeeds", scoped.status().ok()));
                const char *value = std::getenv(std::string(kScopedVariable).c_str());
                static_cast<void>(
                    context.expectTrue("ScopedEnvironmentVariable sets value", value != nullptr && std::string_view(value) == "temporary"));
            }

            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable restores missing state", std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
        }

        {
            TestSupport::ScopedEnvironmentVariable existing(kScopedVariable, "old");
            static_cast<void>(context.expectTrue("Existing environment setup succeeds", existing.status().ok()));
            {
                TestSupport::ScopedEnvironmentVariable nested(kScopedVariable, "new");
                static_cast<void>(context.expectTrue("Nested environment override succeeds", nested.status().ok()));
                const char *value = std::getenv(std::string(kScopedVariable).c_str());
                static_cast<void>(
                    context.expectTrue("ScopedEnvironmentVariable overrides existing value", value != nullptr && std::string_view(value) == "new"));
            }

            const char *restored = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(
                context.expectTrue("ScopedEnvironmentVariable restores old value", restored != nullptr && std::string_view(restored) == "old"));

            {
                TestSupport::ScopedUnsetEnvironmentVariable unset(kScopedVariable);
                static_cast<void>(context.expectTrue("Nested scoped unset succeeds", unset.status().ok()));
                static_cast<void>(context.expectTrue(
                    "ScopedUnsetEnvironmentVariable clears existing value",
                    std::getenv(std::string(kScopedVariable).c_str()) == nullptr));
            }

            const char *afterUnset = std::getenv(std::string(kScopedVariable).c_str());
            static_cast<void>(context.expectTrue(
                "ScopedUnsetEnvironmentVariable restores old value",
                afterUnset != nullptr && std::string_view(afterUnset) == "old"));
        }

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        {
            using FailurePoint = TestSupport::TestHooks::EnvironmentFailurePoint;
            constexpr std::uint64_t nativeCode = 0x3001u;
            TestSupport::TestHooks::reset();

            TestSupport::TestHooks::forceNextEnvironmentFailure(FailurePoint::Read, nativeCode);
            const TestSupport::ScopedEnvironmentVariable readFailure("INTERNAL_TEST_SUPPORT_INJECTED_READ", "value");
            static_cast<void>(context.expectEq("Injected environment read preserves native code", nativeCode, readFailure.status().nativeCode));

            TestSupport::TestHooks::forceNextEnvironmentFailure(FailurePoint::Set, nativeCode);
            const TestSupport::ScopedEnvironmentVariable setFailure("INTERNAL_TEST_SUPPORT_INJECTED_SET", "value");
            static_cast<void>(context.expectEq("Injected environment set preserves native code", nativeCode, setFailure.status().nativeCode));

            TestSupport::TestHooks::forceNextEnvironmentFailure(FailurePoint::Unset, nativeCode);
            const TestSupport::ScopedUnsetEnvironmentVariable unsetFailure("INTERNAL_TEST_SUPPORT_INJECTED_UNSET");
            static_cast<void>(context.expectEq("Injected environment unset preserves native code", nativeCode, unsetFailure.status().nativeCode));

            TestSupport::TestHooks::reset();
        }
#endif
    }

    /// @brief Verifies child launch, merged capture, truncation, timeout, environment, and process-tree cleanup.
    void testChildProcesses(TestSupport::Context &context, std::string_view executablePath, const TestSupportTestOptions &options)
    {
        if (!options.enableChildProcessTests)
        {
            context.skip("TestSupport child process tests", "disabled by TestSupportTestOptions");
            return;
        }

        const auto expectInvalidOptions = [&](std::string_view name, const TestSupport::Types::Process::Options &childOptions)
        {
            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectEq(name, TestSupport::Types::InfrastructureError::InvalidArgument, result.status.error));
            static_cast<void>(context.expectEq(
                std::string(name) + " leaves the child unstarted",
                TestSupport::Types::Process::Outcome::NotStarted,
                result.outcome));
        };

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string("invalid-\0-argument", 18)};
            expectInvalidOptions("Child process rejects embedded-null arguments", childOptions);

            childOptions.arguments = {std::string("\xFF", 1)};
            expectInvalidOptions("Child process rejects invalid UTF-8 arguments", childOptions);

            childOptions.arguments.clear();
            childOptions.environmentOverrides = {{"INVALID=NAME", "value"}};
            expectInvalidOptions("Child process rejects invalid environment names", childOptions);

            childOptions.environmentOverrides = {{"VALID_NAME", std::string("invalid-\0-value", 15)}};
            expectInvalidOptions("Child process rejects embedded-null environment values", childOptions);
        }

        {
            TestSupport::ScopedEnvironmentVariable parentUnset(kChildUnsetVariable, "parent-value");
            static_cast<void>(context.expectTrue("Child environment parent fixture succeeds", parentUnset.status().ok()));

            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kEnvironmentChildArgument)};
            childOptions.environmentOverrides = {
                TestSupport::Types::Process::EnvironmentOverride{std::string(kChildSetVariable), std::string("child-value")},
                TestSupport::Types::Process::EnvironmentOverride{std::string(kChildUnsetVariable), std::nullopt}};
            childOptions.timeout = 5s;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue(
                "Child process environment exits successfully",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
            static_cast<void>(context.expectContains("Child process captures stdout", result.outputBytes, "child-set=child-value"));
            static_cast<void>(context.expectContains("Child process unsets environment", result.outputBytes, "child-unset=<unset>"));
        }

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
            childOptions.captureOutput = false;
            childOptions.timeout = 5s;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue(
                "Child process capture can be disabled",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
            static_cast<void>(context.expectEq("Disabled capture leaves output empty", std::string(), result.outputBytes));
        }

#if defined(_WIN32)
        {
            SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = sizeof(attributes);
            attributes.bInheritHandle = TRUE;
            const HANDLE unrelatedEvent = CreateEventW(&attributes, TRUE, FALSE, nullptr);
            static_cast<void>(context.expectTrue(
                "Create inheritable sentinel handle succeeds",
                unrelatedEvent != nullptr && unrelatedEvent != INVALID_HANDLE_VALUE));
            if (unrelatedEvent != nullptr && unrelatedEvent != INVALID_HANDLE_VALUE)
            {
                TestSupport::Types::Process::Options childOptions;
                childOptions.executablePath = std::filesystem::path(executablePath);
                childOptions.arguments = {
                    std::string(kHandleInheritanceChildArgument),
                    std::to_string(reinterpret_cast<std::uintptr_t>(unrelatedEvent))};
                childOptions.timeout = 5s;

                const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
                static_cast<void>(context.expectTrue(
                    "Handle inheritance probe child succeeds",
                    result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
                static_cast<void>(context.expectEq(
                    "Child does not inherit unrelated parent handles",
                    static_cast<DWORD>(WAIT_TIMEOUT),
                    WaitForSingleObject(unrelatedEvent, 0)));
                CloseHandle(unrelatedEvent);
            }
        }
#endif

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kEnvironmentChildArgument)};
            childOptions.environmentOverrides = {
                TestSupport::Types::Process::EnvironmentOverride{std::string(kChildSetVariable), std::string("child-value")},
                TestSupport::Types::Process::EnvironmentOverride{std::string(kChildUnsetVariable), std::nullopt}};
            childOptions.inheritParentEnvironment = false;
            childOptions.timeout = 5s;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue(
                "Child process can disable parent environment inheritance",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
        }

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kExitCodeChildArgument), "7"};
            childOptions.timeout = 5s;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Normal child exit has successful infrastructure", result.status.ok()));
            static_cast<void>(context.expectEq("Nonzero child reports exited outcome", TestSupport::Types::Process::Outcome::Exited, result.outcome));
            static_cast<void>(context.expectEq("Child process reports nonzero exit", std::uint32_t{7}, result.exitCode));
        }

#if defined(_WIN32)
        {
            struct ExitBoundary
            {
                std::string_view argument;
                std::uint32_t expected;
            };
            constexpr std::array boundaries{
                ExitBoundary{"2147483647", 0x7fffffffu},
                ExitBoundary{"-2147483648", 0x80000000u},
                ExitBoundary{"-1", 0xffffffffu},
            };
            for (const ExitBoundary &boundary : boundaries)
            {
                TestSupport::Types::Process::Options childOptions;
                childOptions.executablePath = std::filesystem::path(executablePath);
                childOptions.arguments = {std::string(kExitCodeChildArgument), std::string(boundary.argument)};
                childOptions.timeout = 5s;

                const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
                static_cast<void>(context.expectTrue("Boundary child exit has successful infrastructure", result.status.ok()));
                static_cast<void>(
                    context.expectEq("Boundary child reports exited outcome", TestSupport::Types::Process::Outcome::Exited, result.outcome));
                static_cast<void>(context.expectEq("Boundary child exit preserves all native bits", boundary.expected, result.exitCode));
            }
        }
#endif

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath).parent_path() / "missing-gamewip-child.exe";
            childOptions.timeout = 5s;
            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectEq(
                "Missing child reports launch failure",
                TestSupport::Types::InfrastructureError::ProcessLaunchFailed,
                result.status.error));
            static_cast<void>(context.expectEq("Missing child was not started", TestSupport::Types::Process::Outcome::NotStarted, result.outcome));
            static_cast<void>(context.expectTrue("Missing child preserves a native diagnostic", result.status.nativeCode != 0));
        }

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kSleepChildArgument)};
            childOptions.timeout = 50ms;

            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Child process timeout has successful infrastructure", result.status.ok()));
            static_cast<void>(context.expectEq("Child process timeout is reported", TestSupport::Types::Process::Outcome::TimedOut, result.outcome));
        }

        const auto runOutputChild = [&](std::size_t outputBytes, std::size_t captureLimit)
        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kOutputChildArgument), std::to_string(outputBytes)};
            childOptions.maxCapturedOutputBytes = captureLimit;
            childOptions.timeout = 5s;
            return TestSupport::runChildProcess(childOptions);
        };

        {
            const TestSupport::Types::Process::Result result = runOutputChild(4, 8);
            static_cast<void>(context.expectTrue(
                "Capture below limit succeeds",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
            static_cast<void>(context.expectEq("Capture below limit retains all bytes", std::size_t{4}, result.outputBytes.size()));
            static_cast<void>(context.expectFalse("Capture below limit is not truncated", result.outputTruncated));
        }

        {
            const TestSupport::Types::Process::Result result = runOutputChild(8, 8);
            static_cast<void>(context.expectEq("Capture at limit retains all bytes", std::size_t{8}, result.outputBytes.size()));
            static_cast<void>(context.expectFalse("Capture at limit is not truncated", result.outputTruncated));
        }

        {
            const TestSupport::Types::Process::Result result = runOutputChild(12, 8);
            static_cast<void>(context.expectTrue(
                "Capture above limit still succeeds",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
            static_cast<void>(context.expectEq("Capture above limit retains prefix", std::string(8, 'x'), result.outputBytes));
            static_cast<void>(context.expectTrue("Capture above limit reports truncation", result.outputTruncated));
        }

        {
            const TestSupport::Types::Process::Result result = runOutputChild(12, 0);
            static_cast<void>(context.expectTrue(
                "Zero capture limit still drains child",
                result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode == 0));
            static_cast<void>(context.expectTrue("Zero capture limit retains no bytes", result.outputBytes.empty()));
            static_cast<void>(context.expectTrue("Zero capture limit reports truncation", result.outputTruncated));
        }

        {
            TestSupport::Types::Process::Options childOptions;
            childOptions.executablePath = std::filesystem::path(executablePath);
            childOptions.arguments = {std::string(kDescendantChildArgument)};
            childOptions.timeout = 50ms;
            const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);
            static_cast<void>(context.expectTrue("Descendant process timeout has successful infrastructure", result.status.ok()));
            static_cast<void>(
                context.expectEq("Descendant process timeout is reported", TestSupport::Types::Process::Outcome::TimedOut, result.outcome));
        }

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        {
            using FailurePoint = TestSupport::TestHooks::ChildProcessFailurePoint;
            struct FailureCase
            {
                FailurePoint point;
                TestSupport::Types::InfrastructureError error;
                TestSupport::Types::Process::Outcome outcome;
            };

            constexpr std::array failureCases{
                FailureCase{
                    FailurePoint::Allocation,
                    TestSupport::Types::InfrastructureError::OutOfMemory,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::Unsupported,
                    TestSupport::Types::InfrastructureError::Unsupported,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::Platform,
                    TestSupport::Types::InfrastructureError::PlatformFailure,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::ProcessSetup,
                    TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::HandleSetup,
                    TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::PipeCreation,
                    TestSupport::Types::InfrastructureError::PipeCreationFailed,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::ProcessLaunch,
                    TestSupport::Types::InfrastructureError::ProcessLaunchFailed,
                    TestSupport::Types::Process::Outcome::NotStarted},
                FailureCase{
                    FailurePoint::JobAssignment,
                    TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                    TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
                FailureCase{
                    FailurePoint::CaptureSetup,
                    TestSupport::Types::InfrastructureError::CaptureFailed,
                    TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
                FailureCase{
                    FailurePoint::ThreadCreation,
                    TestSupport::Types::InfrastructureError::CaptureFailed,
                    TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
                FailureCase{
                    FailurePoint::ThreadResume,
                    TestSupport::Types::InfrastructureError::ProcessSetupFailed,
                    TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
                FailureCase{
                    FailurePoint::CaptureRead,
                    TestSupport::Types::InfrastructureError::CaptureFailed,
                    TestSupport::Types::Process::Outcome::Exited},
                FailureCase{
                    FailurePoint::Wait,
                    TestSupport::Types::InfrastructureError::WaitFailed,
                    TestSupport::Types::Process::Outcome::TerminatedDuringCleanup},
                FailureCase{
                    FailurePoint::ProcessInspection,
                    TestSupport::Types::InfrastructureError::ProcessInspectionFailed,
                    TestSupport::Types::Process::Outcome::OutcomeUnavailable},
                FailureCase{
                    FailurePoint::ProcessCleanup,
                    TestSupport::Types::InfrastructureError::ProcessCleanupFailed,
                    TestSupport::Types::Process::Outcome::Exited},
            };

            TestSupport::TestHooks::reset();
            for (std::size_t index = 0; index < failureCases.size(); ++index)
            {
                const FailureCase &failureCase = failureCases[index];
                const std::uint64_t nativeCode = 0x1000u + index;
                TestSupport::TestHooks::forceNextChildProcessFailure(failureCase.point, nativeCode);

                TestSupport::Types::Process::Options childOptions;
                childOptions.executablePath = std::filesystem::path(executablePath);
                childOptions.arguments = {std::string(kExitCodeChildArgument), "0"};
                childOptions.timeout = 5s;
                const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(childOptions);

                static_cast<void>(context.expectEq("Injected child failure reports its category", failureCase.error, result.status.error));
                static_cast<void>(context.expectEq("Injected child failure preserves native code", nativeCode, result.status.nativeCode));
                static_cast<void>(context.expectEq("Injected child failure reports its outcome", failureCase.outcome, result.outcome));
            }
            TestSupport::TestHooks::reset();
        }
#else
        context.pass("TestSupport child-process hooks skipped because TEST_SUPPORT_INTERNAL_TEST_HOOKS=0");
#endif
    }

    /// @brief Verifies worker coordination, exception propagation, and bounded wait helpers.
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

        /// @brief Assigns each copied worker callable a distinct id and records executed copies.
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

            /// @brief Records the identity of this per-worker callable copy.
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

#include "validation/tests/test_support/utf8_files_test.inl"
} // namespace

namespace GameWIP::Test
{
    int runTestSupportTests(int argc, char **argv, const TestSupportTestOptions &options)
    {
        if (hasArgument(argc, argv, kEnvironmentChildArgument) || hasArgument(argc, argv, kEchoChildArgument) ||
            hasArgument(argc, argv, kSleepChildArgument) || hasArgument(argc, argv, kExitCodeChildArgument) ||
            hasArgument(argc, argv, kOutputChildArgument) || hasArgument(argc, argv, kDescendantChildArgument) ||
            hasArgument(argc, argv, kHandleInheritanceChildArgument))
        {
            return runTestSupportChild(argc, argv);
        }

        const TestSupport::ScopedTemporaryDirectory workspace("test_support_tests");
        if (!workspace.status().ok())
        {
            std::cerr << "TestSupport could not create its test workspace: " << TestSupport::formatInfrastructureStatus(workspace.status()) << '\n';
            return 1;
        }
        const std::filesystem::path &runRoot = workspace.path();

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
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
            "TestSupport UTF-8 text files",
            [&runRoot](TestSupport::Context &context)
            {
                testUtf8TextFileContracts(context, runRoot);
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

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("TestSupport library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        return runner.exitCode();
    }
} // namespace GameWIP::Test
