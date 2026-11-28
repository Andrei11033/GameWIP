/// @file test_support_test.cpp
/// @brief Executable self-tests for the TestSupport library.
///
/// Coverage includes process-global guards, report sinks, child-process cleanup,
/// manual prompts, temporary resources, and deterministic stress primitives.

#include "validation/tests/test_support/test_support_test.h"
#include "validation/tests/test_support/platform.h"

#include "validation/executable_path.h"
#include "validation/platform_tests.h"
#include "validation/process_arguments.h"
#include "validation/tests/internal/helpers/runner_helpers.h"

#include "test_support/test_support.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    namespace TestSupport = GameWIP::TestSupport;
    using GameWIP::Test::TestSupportTestOptions;
    using namespace std::chrono_literals;

    constexpr std::string_view kEnvironmentChildArgument = "--test-support-test-child=environment";
    constexpr std::string_view kCurrentPathChildArgument = "--test-support-test-child=current-path";
    constexpr std::string_view kEchoChildArgument = "--test-support-test-child=echo";
    constexpr std::string_view kSleepChildArgument = "--test-support-test-child=sleep";
    constexpr std::string_view kExitCodeChildArgument = "--test-support-test-child=exit-code";
    constexpr std::string_view kOutputChildArgument = "--test-support-test-child=output";
    constexpr std::string_view kDescendantChildArgument = "--test-support-test-child=descendant";
    constexpr std::string_view kHandleInheritanceChildArgument = "--test-support-test-child=handle-inheritance";
    constexpr std::string_view kStartGateChildArgument = "--test-support-test-child=start-gate";
    constexpr std::string_view kChildSetVariable = "INTERNAL_TEST_SUPPORT_CHILD_SET";
    constexpr std::string_view kChildUnsetVariable = "INTERNAL_TEST_SUPPORT_CHILD_UNSET";
    constexpr std::string_view kScopedVariable = "INTERNAL_TEST_SUPPORT_SCOPED_ENV";

    std::string g_consoleWriterCategory;
    std::string g_consoleWriterLine;

    /// ------------------------------------------------------------
    /// Argument and reporting helpers
    /// ------------------------------------------------------------

    bool captureConsoleWriter(std::string_view category, std::string_view line) noexcept
    {
        try
        {
            if(category == "PASS")
            {
                g_consoleWriterCategory = category;
                g_consoleWriterLine = line;
            }
            return true;
        }
        catch(...)
        {
            return false;
        }
    }

    bool failConsoleWriter(std::string_view, std::string_view) noexcept
    {
        return false;
    }

    /// @brief Returns the argument immediately following a named option.
    std::string argumentAfter(int argc, char **argv, std::string_view argument)
    {
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);

        for(std::size_t index = 1; index + 1 < arguments.size(); ++index)
        {
            if(arguments[index] != nullptr && std::string_view(arguments[index]) == argument && arguments[index + 1] != nullptr)
            {
                return arguments[index + 1];
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
        std::ostringstream error;
        std::streambuf *previousInput = nullptr;
        std::streambuf *previousOutput = nullptr;
        std::streambuf *previousError = nullptr;

        /// @brief Replaces standard prompt streams with deterministic string streams.
        explicit ScopedPromptStreams(std::string_view text)
            : input(std::string(text))
        {
            previousInput = std::cin.rdbuf(input.rdbuf());
            previousOutput = std::cout.rdbuf(output.rdbuf());
            previousError = std::cerr.rdbuf(error.rdbuf());
        }

        ~ScopedPromptStreams()
        {
            std::cin.rdbuf(previousInput);
            std::cout.rdbuf(previousOutput);
            std::cerr.rdbuf(previousError);
        }

        ScopedPromptStreams(const ScopedPromptStreams &) = delete;
        ScopedPromptStreams &operator=(const ScopedPromptStreams &) = delete;
    };

    /// @brief Runs one manual prompt against deterministic captured input.
    TestSupport::Types::Reporting::ManualAnswer promptWithInput(std::string_view input)
    {
        const ScopedPromptStreams streams(input);
        return TestSupport::promptManualCheck("automated prompt check");
    }

#include "validation/tests/test_support/child_modes_test.inl"
#include "validation/tests/test_support/context_test.inl"
#include "validation/tests/test_support/environment_test.inl"
#include "validation/tests/test_support/files_test.inl"
#include "validation/tests/test_support/process_test.inl"
#include "validation/tests/test_support/prompt_test.inl"
#include "validation/tests/test_support/report_modes_test.inl"
#include "validation/tests/test_support/runner_test.inl"
#include "validation/tests/test_support/stress_test.inl"
} // namespace

namespace GameWIP::Test::Internal
{
    /// ------------------------------------------------------------
    /// Private child-mode routing
    /// ------------------------------------------------------------

    bool hasTestSupportChildArguments(int argc, char **argv)
    {
        constexpr std::string_view selectors[] = {
            "--test-support-test-child=environment",
            "--test-support-test-child=current-path",
            "--test-support-test-child=echo",
            "--test-support-test-child=sleep",
            "--test-support-test-child=exit-code",
            "--test-support-test-child=output",
            "--test-support-test-child=descendant",
            "--test-support-test-child=handle-inheritance",
            "--test-support-test-child=start-gate",
        };
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);

        for(const char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if(value != nullptr && std::ranges::find(selectors, std::string_view(value)) != std::end(selectors))
            {
                return true;
            }
        }

        return false;
    }
} // namespace GameWIP::Test::Internal

namespace GameWIP::Test
{
    /// ------------------------------------------------------------
    /// Test runner
    /// ------------------------------------------------------------

    int runTestSupportTests(int argc, char **argv, const TestSupportTestOptions &options)
    {
        // Route private child protocols before creating the test workspace.
        if(Internal::hasTestSupportChildArguments(argc, argv))
        {
            return runTestSupportChild(argc, argv);
        }

        // Prepare an isolated workspace and shared report sink.
        const TestSupport::ScopedTemporaryDirectory workspace("test_support_tests");

        if(!workspace.status().ok())
        {
            std::cerr << "TestSupport could not create its test workspace: " << TestSupport::formatStatus(workspace.status()) << '\n';
            return 1;
        }
        const std::filesystem::path &runRoot = workspace.path();

        const TestSupport::Types::Reporting::Options reportOptions = GameWIP::Test::Internal::RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        runner.info(std::format("TestSupport library test root: {}", GameWIP::Test::Internal::RunnerHelpers::pathText(runRoot)));
        runner.info(
            std::format(
                "TestSupport test options: childProcess={} stress={} report={}",
                options.enableChildProcessTests,
                options.enableStressTests,
                options.writeReport ? GameWIP::Test::Internal::RunnerHelpers::pathText(options.reportPath) : std::string_view{"disabled"}));

        const std::filesystem::path executablePath = GameWIP::Validation::currentExecutablePath();

        // Run each TestSupport feature suite independently.
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
            [&options, &executablePath](TestSupport::Context &context)
            {
                testStressHelpers(context, executablePath, options);
            });

        // Report aggregate outcomes and finalize the shared sink.
        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("TestSupport library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));

        return GameWIP::Test::Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
