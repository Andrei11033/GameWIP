/// @file reporting_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

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
        context.expectEq("ReportOptions default path", std::filesystem::path("logs/validation/latest_test_report.txt"), defaultOptions.reportPath));

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
    static_cast<void>(context.expectTrue("promptManualCheck accepts y", promptWithInput("y\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
    static_cast<void>(context.expectTrue("promptManualCheck accepts no", promptWithInput("no\n") == TestSupport::Types::Reporting::ManualAnswer::No));
    static_cast<void>(context.expectTrue("promptManualCheck accepts n", promptWithInput("n\n") == TestSupport::Types::Reporting::ManualAnswer::No));
    static_cast<void>(
        context.expectTrue("promptManualCheck accepts skipped", promptWithInput("skip\n") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
    static_cast<void>(
        context.expectTrue("promptManualCheck accepts s", promptWithInput("s\n") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
    static_cast<void>(context.expectTrue(
        "promptManualCheck retries invalid input",
        promptWithInput("maybe\nYes\n") == TestSupport::Types::Reporting::ManualAnswer::Yes));
    static_cast<void>(
        context.expectTrue("promptManualCheck returns skipped on EOF", promptWithInput("") == TestSupport::Types::Reporting::ManualAnswer::Skipped));
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
    static_cast<void>(context.expectContains("Runner report includes result", reportResult.text, "[RESULT] FirstSuite passed=1 failed=0 skipped=1"));
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
