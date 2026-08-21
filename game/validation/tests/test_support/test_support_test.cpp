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

#include "validation/tests/test_support/reporting_test.inl"
#include "validation/tests/test_support/files_test.inl"
#include "validation/tests/test_support/environment_test.inl"
#include "validation/tests/test_support/process_test.inl"
#include "validation/tests/test_support/stress_test.inl"
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
