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
#include <span>
#include <sstream>
#include <stdexcept>
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
        std::vector<std::string_view> invocationOrder;
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

    void recordInvocation(ProbeRecord &record, std::string_view name, const ValidationTests::ModuleInvocation &invocation)
    {
        ++record.runs;
        record.manualUi = invocation.options.enableManualUiTests;
        record.loggerPopup = invocation.options.enableLoggerPopupTest;
        record.childProcesses = invocation.options.enableTestSupportChildProcessTests;
        record.writeReport = invocation.options.writeReport;
        probeState.invocationOrder.push_back(name);
    }

    int runAlpha(const ValidationTests::ModuleInvocation &invocation)
    {
        recordInvocation(probeState.alpha, "alpha", invocation);
        return 0;
    }

    int runBeta(const ValidationTests::ModuleInvocation &invocation)
    {
        recordInvocation(probeState.beta, "beta", invocation);
        return 0;
    }

    int runFailure(const ValidationTests::ModuleInvocation &)
    {
        probeState.invocationOrder.push_back("failure");
        return 17;
    }

    int runException(const ValidationTests::ModuleInvocation &)
    {
        probeState.invocationOrder.push_back("exception");
        throw std::runtime_error("expected probe exception");
    }

    int runRecovery(const ValidationTests::ModuleInvocation &)
    {
        probeState.invocationOrder.push_back("recovery");
        return 0;
    }

    int runChildAlpha(const ValidationTests::ModuleInvocation &invocation)
    {
        recordInvocation(probeState.alpha, "child-alpha", invocation);
        return 23;
    }

    [[nodiscard]] bool hasProbeArgument(int argc, char **argv, std::string_view expected) noexcept
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv != nullptr && argv[index] != nullptr && std::string_view(argv[index]) == expected)
            {
                return true;
            }
        }
        return false;
    }

    bool matchesChildAlpha(int argc, char **argv)
    {
        return hasProbeArgument(argc, argv, "--probe-child=alpha") || hasProbeArgument(argc, argv, "--probe-child=ambiguous");
    }

    bool matchesChildBeta(int argc, char **argv)
    {
        return hasProbeArgument(argc, argv, "--probe-child=beta") || hasProbeArgument(argc, argv, "--probe-child=ambiguous");
    }

    bool throwsChildMatcher(int argc, char **argv)
    {
        if (hasProbeArgument(argc, argv, "--probe-child=throw"))
        {
            throw std::runtime_error("expected matcher exception");
        }
        return false;
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

    Validation::TestResult runProbeWithModules(
        std::initializer_list<std::string_view> arguments,
        std::span<const ValidationTests::Module> modules,
        ValidationTests::RunOptions options = unattendedOptions())
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
            modules);
    }

    Validation::TestResult runProbe(std::initializer_list<std::string_view> arguments, ValidationTests::RunOptions options = unattendedOptions())
    {
        return runProbeWithModules(arguments, probeModules, std::move(options));
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
        static_cast<void>(context.expectTrue(
            "default runner uses stable order then name",
            probeState.invocationOrder == std::vector<std::string_view>{"beta", "alpha"}));
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

    void testModuleSkipSelection(TestSupport::Context &context)
    {
        Validation::TestResult result = runProbe({"--skip-test-module=alpha"});
        static_cast<void>(context.expectTrue("single skipped module invocation succeeds", result.ok()));
        static_cast<void>(context.expectEq("single skipped module leaves one module selected", std::size_t{1}, result.modulesRun));
        static_cast<void>(context.expectEq("skipped module is not invoked", std::size_t{0}, probeState.alpha.runs));
        static_cast<void>(context.expectEq("unskipped module is invoked", std::size_t{1}, probeState.beta.runs));

        result = runProbe({"--test-module=alpha", "--skip-test-module=beta"});
        static_cast<void>(context.expectTrue("selected module can skip unrelated module", result.ok()));
        static_cast<void>(context.expectEq("selection plus unrelated skip runs selected module", std::size_t{1}, result.modulesRun));
        static_cast<void>(context.expectEq("selected module still runs", std::size_t{1}, probeState.alpha.runs));
        static_cast<void>(context.expectEq("unrelated skipped module does not run", std::size_t{0}, probeState.beta.runs));

        result = runProbe({"--skip-test-module=gamma"});
        static_cast<void>(context.expectFalse("unknown skipped module fails", result.ok()));
        static_cast<void>(context.expectEq("unknown skipped module runs no module", std::size_t{0}, result.modulesRun));

        result = runProbe({"--test-module=alpha", "--skip-test-module=alpha"});
        static_cast<void>(context.expectFalse("selected and skipped module fails", result.ok()));
        static_cast<void>(context.expectEq("selected and skipped module runs no module", std::size_t{0}, result.modulesRun));

        result = runProbe({"--skip-test-module=alpha", "--skip-test-module=beta"});
        static_cast<void>(context.expectFalse("skipping every module fails", result.ok()));
        static_cast<void>(context.expectEq("skipping every module runs no module", std::size_t{0}, result.modulesRun));
    }

    void testInvalidModuleSelection(TestSupport::Context &context)
    {
        Validation::TestResult result = runProbe({"--test-module=gamma"});
        static_cast<void>(context.expectFalse("unknown selected module fails", result.ok()));
        static_cast<void>(context.expectEq("unknown selected module runs no module", std::size_t{0}, result.modulesRun));

        result = runProbe({"--test-module="});
        static_cast<void>(context.expectFalse("empty selected module fails", result.ok()));
        static_cast<void>(context.expectEq("empty selected module runs no module", std::size_t{0}, result.modulesRun));

        result = runProbe({"--test-module=alpha", "--test-module=beta"});
        static_cast<void>(context.expectFalse("conflicting selected modules fail", result.ok()));
        static_cast<void>(context.expectEq("conflicting selected modules run no module", std::size_t{0}, result.modulesRun));

        result = runProbe({"--test-module=alpha", "--test-module=alpha"});
        static_cast<void>(context.expectTrue("repeated identical module selector succeeds", result.ok()));
        static_cast<void>(context.expectEq("repeated identical selector runs one module", std::size_t{1}, result.modulesRun));
        static_cast<void>(context.expectEq("repeated identical selector invokes alpha once", std::size_t{1}, probeState.alpha.runs));
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

    void testModuleFailureIsolation(TestSupport::Context &context)
    {
        constexpr std::array<ValidationTests::Module, 3> failureModules = {{
            {.name = "failure", .order = 10, .run = runFailure},
            {.name = "exception", .order = 20, .run = runException},
            {.name = "recovery", .order = 30, .run = runRecovery},
        }};

        const Validation::TestResult result = runProbeWithModules({}, failureModules);
        static_cast<void>(context.expectFalse("module failures fail the aggregate", result.ok()));
        static_cast<void>(context.expectEq("module failures still run every selected module", std::size_t{3}, result.modulesRun));
        static_cast<void>(context.expectEq("return and exception failures are both counted", std::size_t{2}, result.modulesFailed));
        static_cast<void>(context.expectEq("normal aggregate failures use exit code one", 1, result.exitCode));
        static_cast<void>(context.expectFalse("ordinary module failures are not child routes", result.handledChildInvocation));
        static_cast<void>(context.expectTrue(
            "runner continues in deterministic order after failures",
            probeState.invocationOrder == std::vector<std::string_view>{"failure", "exception", "recovery"}));
    }

    void testInvalidRegistrations(TestSupport::Context &context)
    {
        constexpr std::array<ValidationTests::Module, 1> emptyName = {{
            {.name = "", .order = 10, .run = runAlpha},
        }};
        Validation::TestResult result = runProbeWithModules({}, emptyName);
        static_cast<void>(context.expectFalse("empty module name fails registration validation", result.ok()));
        static_cast<void>(context.expectEq("empty module name runs no module", std::size_t{0}, result.modulesRun));

        constexpr std::array<ValidationTests::Module, 1> nullCallback = {{
            {.name = "null", .order = 10, .run = nullptr},
        }};
        result = runProbeWithModules({}, nullCallback);
        static_cast<void>(context.expectFalse("null module callback fails registration validation", result.ok()));
        static_cast<void>(context.expectEq("null module callback runs no module", std::size_t{0}, result.modulesRun));

        constexpr std::array<ValidationTests::Module, 2> duplicateNames = {{
            {.name = "duplicate", .order = 10, .run = runAlpha},
            {.name = "duplicate", .order = 20, .run = runBeta},
        }};
        result = runProbeWithModules({}, duplicateNames);
        static_cast<void>(context.expectFalse("duplicate module names fail registration validation", result.ok()));
        static_cast<void>(context.expectEq("duplicate module names run no module", std::size_t{0}, result.modulesRun));
    }

    void testChildRouting(TestSupport::Context &context)
    {
        constexpr std::array<ValidationTests::Module, 2> childModules = {{
            {.name = "child-alpha", .order = 10, .run = runChildAlpha, .handlesChildArguments = matchesChildAlpha},
            {.name = "child-beta", .order = 20, .run = runBeta, .handlesChildArguments = matchesChildBeta},
        }};

        Validation::TestResult result = runProbeWithModules({"--probe-child=alpha"}, childModules);
        static_cast<void>(context.expectFalse("nonzero child route is a failed result", result.ok()));
        static_cast<void>(context.expectTrue("matched child route is handled", result.handledChildInvocation));
        static_cast<void>(context.expectEq("matched child route runs only its owner", std::size_t{1}, result.modulesRun));
        static_cast<void>(context.expectEq("matched child route preserves exact exit code", 23, result.exitCode));
        static_cast<void>(context.expectEq("matched child route invokes alpha once", std::size_t{1}, probeState.alpha.runs));
        static_cast<void>(context.expectEq("matched child route does not invoke beta", std::size_t{0}, probeState.beta.runs));

        result = runProbeWithModules({"--probe-child=ambiguous"}, childModules);
        static_cast<void>(context.expectFalse("ambiguous child ownership fails", result.ok()));
        static_cast<void>(context.expectTrue("ambiguous child ownership is handled", result.handledChildInvocation));
        static_cast<void>(context.expectEq("ambiguous child ownership runs no module", std::size_t{0}, result.modulesRun));

        constexpr std::array<ValidationTests::Module, 1> throwingMatcher = {{
            {.name = "throwing-matcher", .order = 10, .run = runAlpha, .handlesChildArguments = throwsChildMatcher},
        }};
        result = runProbeWithModules({"--probe-child=throw"}, throwingMatcher);
        static_cast<void>(context.expectFalse("child matcher exception fails validation", result.ok()));
        static_cast<void>(context.expectEq("child matcher exception runs no module", std::size_t{0}, result.modulesRun));
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
        runner.runSuite("Validation runner module skips", testModuleSkipSelection);
        runner.runSuite("Validation runner invalid module selection", testInvalidModuleSelection);
        runner.runSuite("Validation runner removed and retained options", testRemovedAndRetainedOptions);
        runner.runSuite("Validation runner reserved input validation", testReservedChildAndReportValidation);
        runner.runSuite("Validation runner failure isolation", testModuleFailureIsolation);
        runner.runSuite("Validation runner invalid registrations", testInvalidRegistrations);
        runner.runSuite("Validation runner child routing", testChildRouting);
        runner.runSuite("Validation runner embedded request", testEmbeddedRunRequest);

        const TestSupport::Types::Summary result = runner.result();
        runner.summary(std::format("Validation runner tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        return runner.exitCode();
    }
} // namespace GameWIP::Test
