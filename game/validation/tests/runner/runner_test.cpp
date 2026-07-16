/// @file runner_test.cpp
/// @brief Tests validation-runner option parsing, selection, and module propagation.
///
/// Probe modules are supplied through the approved runner seam so parsing,
/// selection, ordering, and option propagation can be tested deterministically.

#include "validation/tests/runner/runner_test.h"

#include "test_support/test_support.h"
#include "validation/tests/internal/runner_test_hooks.h"

#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace TestSupport = GameWIP::TestSupport;
    namespace Validation = GameWIP::Validation;
    namespace ValidationTests = GameWIP::Validation::Tests;

    struct ProbeRecord
    {
        std::size_t runs = 0;
        bool manualUi = false;
        bool loggerPopup = false;
        bool childProcesses = true;
        bool writeReport = true;
    };

    struct ProbeState
    {
        ProbeRecord alpha;
        ProbeRecord beta;
    };

    ProbeState probeState;

    /// @brief Suppresses expected nested-runner diagnostics during option tests.
    class ScopedRunnerOutput final
    {
    public:
        ScopedRunnerOutput()
            : previousOutput_(std::cout.rdbuf(output_.rdbuf()))
            , previousError_(std::cerr.rdbuf(error_.rdbuf()))
        {
        }

        ScopedRunnerOutput(const ScopedRunnerOutput &) = delete;
        ScopedRunnerOutput &operator=(const ScopedRunnerOutput &) = delete;

        ~ScopedRunnerOutput()
        {
            std::cout.rdbuf(previousOutput_);
            std::cerr.rdbuf(previousError_);
        }

    private:
        std::ostringstream output_;
        std::ostringstream error_;
        std::streambuf *previousOutput_ = nullptr;
        std::streambuf *previousError_ = nullptr;
    };

    void recordInvocation(ProbeRecord &record, const ValidationTests::ModuleInvocation &invocation)
    {
        ++record.runs;
        record.manualUi = invocation.options.enableManualUiTests;
        record.loggerPopup = invocation.options.enableLoggerPopupTest;
        record.childProcesses = invocation.options.enableTestSupportChildProcessTests;
        record.writeReport = invocation.options.writeReport;
    }

    int runAlpha(const ValidationTests::ModuleInvocation &invocation)
    {
        recordInvocation(probeState.alpha, invocation);
        return 0;
    }

    int runBeta(const ValidationTests::ModuleInvocation &invocation)
    {
        recordInvocation(probeState.beta, invocation);
        return 0;
    }

    constexpr std::array<ValidationTests::Module, 2> probeModules = {{
        {.name = "alpha", .order = 20, .run = runAlpha},
        {.name = "beta", .order = 10, .run = runBeta},
    }};

    ValidationTests::RunOptions unattendedOptions()
    {
        ValidationTests::RunOptions options;
        options.writeReport = false;
        return options;
    }

    Validation::TestResult runProbe(std::initializer_list<std::string_view> arguments, ValidationTests::RunOptions options = unattendedOptions())
    {
        probeState = {};

        std::vector<std::string> argumentStorage;
        argumentStorage.reserve(arguments.size() + 1);
        argumentStorage.emplace_back("runner-tests");
        for (const std::string_view argument : arguments)
        {
            argumentStorage.emplace_back(argument);
        }

        std::vector<char *> argumentPointers;
        argumentPointers.reserve(argumentStorage.size());
        for (std::string &argument : argumentStorage)
        {
            argumentPointers.push_back(argument.data());
        }

        ScopedRunnerOutput capturedOutput;
        return ValidationTests::Detail::runWithModules(
            static_cast<int>(argumentPointers.size()),
            argumentPointers.data(),
            std::move(options),
            probeModules);
    }

    void testDefaultManualOptions(TestSupport::Context &context)
    {
        const Validation::TestResult result = runProbe({});
        static_cast<void>(context.expectTrue("default runner invocation succeeds", result.ok()));
        static_cast<void>(context.expectEq("default runner keeps both modules selected", std::size_t{2}, result.modulesRun));
        static_cast<void>(context.expectEq("default runner invokes alpha", std::size_t{1}, probeState.alpha.runs));
        static_cast<void>(context.expectEq("default runner invokes beta", std::size_t{1}, probeState.beta.runs));
        static_cast<void>(context.expectFalse("manual UI defaults off", probeState.alpha.manualUi));
        static_cast<void>(context.expectFalse("Logger popup defaults off", probeState.alpha.loggerPopup));
    }

    void testPositiveCapabilityOptions(TestSupport::Context &context)
    {
        Validation::TestResult result = runProbe({"--manual-ui"});
        static_cast<void>(context.expectTrue("manual UI runner invocation succeeds", result.ok()));
        static_cast<void>(context.expectEq("manual UI does not focus a module", std::size_t{2}, result.modulesRun));
        static_cast<void>(context.expectTrue("manual UI propagates to alpha", probeState.alpha.manualUi));
        static_cast<void>(context.expectTrue("manual UI propagates to beta", probeState.beta.manualUi));
        static_cast<void>(context.expectFalse("manual UI does not enable Logger popup", probeState.alpha.loggerPopup));

        result = runProbe({"--logger-popup"});
        static_cast<void>(context.expectTrue("Logger popup runner invocation succeeds", result.ok()));
        static_cast<void>(context.expectEq("Logger popup does not focus a module", std::size_t{2}, result.modulesRun));
        static_cast<void>(context.expectTrue("Logger popup propagates to alpha", probeState.alpha.loggerPopup));
        static_cast<void>(context.expectTrue("Logger popup propagates to beta", probeState.beta.loggerPopup));
        static_cast<void>(context.expectFalse("Logger popup does not enable general manual UI", probeState.alpha.manualUi));
    }

    void testSelectionIndependence(TestSupport::Context &context)
    {
        const Validation::TestResult result = runProbe({"--manual-ui", "--logger-popup", "--test-module=alpha"});
        static_cast<void>(context.expectTrue("focused capability invocation succeeds", result.ok()));
        static_cast<void>(context.expectEq("explicit selector runs one module", std::size_t{1}, result.modulesRun));
        static_cast<void>(context.expectEq("explicit selector invokes alpha", std::size_t{1}, probeState.alpha.runs));
        static_cast<void>(context.expectEq("capability flags do not invoke beta", std::size_t{0}, probeState.beta.runs));
        static_cast<void>(context.expectTrue("focused module receives manual UI", probeState.alpha.manualUi));
        static_cast<void>(context.expectTrue("focused module receives Logger popup", probeState.alpha.loggerPopup));
    }

    void testRemovedAndRetainedOptions(TestSupport::Context &context)
    {
        ValidationTests::RunOptions enabledOptions = unattendedOptions();
        enabledOptions.enableManualUiTests = true;
        enabledOptions.enableLoggerPopupTest = true;
        Validation::TestResult result = runProbe({"--no-manual-ui", "--no-logger-popup", "--test-support-manual"}, std::move(enabledOptions));
        static_cast<void>(context.expectTrue("removed options are ignored", result.ok()));
        static_cast<void>(context.expectEq("removed TestSupport alias does not select a module", std::size_t{2}, result.modulesRun));
        static_cast<void>(context.expectTrue("removed manual disable option has no effect", probeState.alpha.manualUi));
        static_cast<void>(context.expectTrue("removed popup disable option has no effect", probeState.alpha.loggerPopup));

        result = runProbe({"--no-test-report", "--no-test-support-child-process"});
        static_cast<void>(context.expectTrue("retained negative options succeed", result.ok()));
        static_cast<void>(context.expectFalse("no-test-report propagates", probeState.alpha.writeReport));
        static_cast<void>(context.expectFalse("no-test-support-child-process propagates", probeState.alpha.childProcesses));
    }

    void testReservedChildAndReportValidation(TestSupport::Context &context)
    {
        Validation::TestResult result = runProbe({"--assert-test-child=unknown"});
        static_cast<void>(context.expectFalse("unknown reserved child selector fails", result.ok()));
        static_cast<void>(context.expectTrue("unknown reserved child selector is handled", result.handledChildInvocation));
        static_cast<void>(context.expectEq("unknown reserved child selector runs no module", std::size_t{0}, result.modulesRun));

        result = runProbe({"--assert-test-child=unknown", "--test-support-test-child=unknown"});
        static_cast<void>(context.expectFalse("multiple reserved child selectors fail", result.ok()));
        static_cast<void>(context.expectTrue("multiple reserved child selectors are handled", result.handledChildInvocation));
        static_cast<void>(context.expectEq("multiple reserved child selectors run no module", std::size_t{0}, result.modulesRun));

#if defined(_WIN32)
        ValidationTests::RunOptions rootRelative = unattendedOptions();
        rootRelative.writeReport = true;
        rootRelative.reportPath = R"(\escaped-report.txt)";
        result = runProbe({}, std::move(rootRelative));
        static_cast<void>(context.expectTrue("root-relative report rejection does not fail tests", result.ok()));
        static_cast<void>(context.expectFalse("root-relative report path disables report output", probeState.alpha.writeReport));

        ValidationTests::RunOptions driveRelative = unattendedOptions();
        driveRelative.writeReport = true;
        driveRelative.reportPath = "C:escaped-report.txt";
        result = runProbe({}, std::move(driveRelative));
        static_cast<void>(context.expectTrue("drive-relative report rejection does not fail tests", result.ok()));
        static_cast<void>(context.expectFalse("drive-relative report path disables report output", probeState.alpha.writeReport));
#endif
    }

    void testEmbeddedRunRequest(TestSupport::Context &context)
    {
        std::string executable = "gamewip.exe";
        std::string startupTests = "--startup-tests";
        std::string version = "--version";

        std::array<char *, 2> startupArguments = {executable.data(), startupTests.data()};
        std::array<char *, 2> ordinaryArguments = {executable.data(), version.data()};

        static_cast<void>(context.expectTrue(
            "startup-tests requests embedded validation",
            ValidationTests::requestsRun(static_cast<int>(startupArguments.size()), startupArguments.data())));
        static_cast<void>(context.expectFalse(
            "ordinary game arguments do not request embedded validation",
            ValidationTests::requestsRun(static_cast<int>(ordinaryArguments.size()), ordinaryArguments.data())));
        static_cast<void>(context.expectFalse("empty arguments do not request embedded validation", ValidationTests::requestsRun(0, nullptr)));
    }
} // namespace

namespace GameWIP::Test
{
    int runRunnerTests(const RunnerTestOptions &options)
    {
        TestSupport::Types::ReportOptions reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::ConsoleVerbosity::Full : TestSupport::Types::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        runner.runSuite("Validation runner default options", testDefaultManualOptions);
        runner.runSuite("Validation runner capability options", testPositiveCapabilityOptions);
        runner.runSuite("Validation runner selection independence", testSelectionIndependence);
        runner.runSuite("Validation runner removed and retained options", testRemovedAndRetainedOptions);
        runner.runSuite("Validation runner reserved input validation", testReservedChildAndReportValidation);
        runner.runSuite("Validation runner embedded request", testEmbeddedRunRequest);

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Validation runner tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
