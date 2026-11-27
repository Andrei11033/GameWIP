/// @file context_test.inl
/// @brief Private TestSupport summary and context tests.

/// ------------------------------------------------------------
/// Summary and context
/// ------------------------------------------------------------

/// @brief Verifies summary arithmetic, result predicates, defaults, and timer units.
void testSummaryAndTimer(TestSupport::Context &context)
{
    const TestSupport::Types::Reporting::Options defaultOptions;
    static_cast<void>(context.expectTrue("ReportOptions default console", defaultOptions.writeConsole));
    static_cast<void>(context.expectTrue("ReportOptions default report", defaultOptions.writeReport));
    static_cast<void>(context.expectFalse("ReportOptions default append", defaultOptions.appendReport));
    static_cast<void>(context.expectFalse("ReportOptions default per-line flush", defaultOptions.flushReportEachLine));
    static_cast<void>(context.expectTrue("ReportOptions default console writer is unset", defaultOptions.consoleWriter == nullptr));
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

    static_cast<void>(context.expectEq("Infrastructure success has a stable diagnostic", std::string("Success"), TestSupport::formatStatus({})));
    static_cast<void>(context.expectEq(
        "Infrastructure failure diagnostic preserves category and native code",
        std::string("FileOperationFailed (nativeCode=Other:42)"),
        TestSupport::formatStatus(
            GameWIP::Status::Types::Status::withDiagnostic(
                GameWIP::Status::Types::Code::FileOperationFailed,
                GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, 42}))));

    TestSupport::Timer timer;
    static_cast<void>(context.expectTrue("Timer elapsed non-negative", timer.elapsedMilliseconds() >= 0.0));
    timer.reset();
    std::this_thread::sleep_for(1ms);
    static_cast<void>(context.expectTrue("Timer elapsed increases", timer.elapsedMilliseconds() > 0.0));
}

/// @brief Verifies Context categories, expectation diagnostics, counts, and report failures.
void testContextReporting(TestSupport::Context &context, const std::filesystem::path &root)
{
    // Record normal categories and successful expectations.
    const std::filesystem::path reportPath = root / "context_report.txt";
    TestSupport::Context nested("NestedContext", quietReport(reportPath));
    static_cast<void>(
        context.expectEq("Standalone context report starts active", TestSupport::Types::Reporting::SinkState::Active, nested.reportStatus().state));
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

    const TestSupport::Types::Reporting::SinkStatus nestedFinalStatus = nested.finalize();
    static_cast<void>(
        context.expectEq("Standalone context finalization succeeds", TestSupport::Types::Reporting::SinkState::Finalized, nestedFinalStatus.state));
    static_cast<void>(context.expectTrue("Standalone context finalization has no failure", nestedFinalStatus.failureStatus.ok()));

    const TestSupport::Types::TextResult reportResult = TestSupport::readTextFile(reportPath);
    static_cast<void>(context.expectTrue("Context report read succeeds", reportResult.ok()));
    if(reportResult.ok())
    {
        const std::string &report = reportResult.value();
        static_cast<void>(context.expectContains("Context report includes info", report, "[INFO] [NestedContext] info line"));
        static_cast<void>(context.expectContains("Context report includes pass", report, "[PASS] [NestedContext] direct pass"));
        static_cast<void>(context.expectContains("Context report includes skip", report, "[SKIP] [NestedContext] direct skip: not needed"));
        static_cast<void>(context.expectContains("Context report includes manual", report, "[MANUAL] [NestedContext] manual line"));
        static_cast<void>(context.expectContains("Context report includes metric", report, "[METRIC] [NestedContext] metric line"));
    }

    // Keep failures countable and their value formatting inspectable.
    TestSupport::Context failing("FailingContext", quietReport(root / "failing_context_report.txt"));
    static_cast<void>(failing.expectTrue("intentional false expectation", false));
    static_cast<void>(failing.expectNear("intentional negative tolerance", 1.0, 1.0, -1.0));
    static_cast<void>(failing.expectNear("intentional outside tolerance", 1.0, 2.0, 0.1));
    static_cast<void>(failing.expectFileContains("intentional missing file", root / "missing.txt", "needle"));
    static_cast<void>(
        failing.expectEq("intentional enum mismatch", TestSupport::Types::Process::Outcome::Exited, TestSupport::Types::Process::Outcome::TimedOut));
    enum class SignedDiagnostic : int
    {
        Negative = -7,
        Positive = 42
    };
    static_cast<void>(failing.expectEq("intentional signed enum mismatch", SignedDiagnostic::Negative, SignedDiagnostic::Positive));
    const TestSupport::Types::Reporting::Summary failingSummary = failing.result();
    static_cast<void>(context.expectEq("Context records failures without throwing", std::size_t{6}, failingSummary.failed));
    static_cast<void>(
        context.expectFileContains("enum mismatch shows narrow numeric values", root / "failing_context_report.txt", "expected 1, got 2"));
    static_cast<void>(
        context.expectFileContains("enum mismatch preserves negative values", root / "failing_context_report.txt", "expected -7, got 42"));
    static_cast<void>(context.expectFalse("Failing context not ok", failing.ok()));
    const TestSupport::Types::Reporting::SinkStatus failingFinalStatus = failing.finalize();
    static_cast<void>(
        context.expectEq("Failing context report still finalizes", TestSupport::Types::Reporting::SinkState::Finalized, failingFinalStatus.state));
    context.pass("Context continued after nested failures");
}
