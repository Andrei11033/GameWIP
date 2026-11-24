/// @file runner_test.inl
/// @brief Private TestSupport runner and section tests.

/// ------------------------------------------------------------
/// Runner and result sections
/// ------------------------------------------------------------

/// @brief Verifies Runner aggregation, exception capture, and Section diagnostics.
void testRunnerAndSection(TestSupport::Context &context, const std::filesystem::path &root)
{
    // Aggregate suite results and verify the shared report lifecycle.
    TestSupport::Types::Reporting::Options runnerOptions = quietReport(root / "runner_report.txt");
    runnerOptions.flushReportEachLine = false;
    TestSupport::Runner runner(runnerOptions);
    bool runnerOwnedContextStayedActive = false;
    const TestSupport::Types::Reporting::SuiteResult first = runner.runSuite(
        "FirstSuite",
        [&runnerOwnedContextStayedActive](TestSupport::Context &suite)
        {
            const TestSupport::Section section(suite, "small section");
            suite.pass("suite pass");
            suite.skip("suite skip", "not applicable");
            runnerOwnedContextStayedActive = suite.finalize().state == TestSupport::Types::Reporting::SinkState::Active;
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
    static_cast<void>(context.expectTrue("Runner-owned context cannot finalize shared sink", runnerOwnedContextStayedActive));
    static_cast<void>(context.expectEq("Runner report starts active", TestSupport::Types::Reporting::SinkState::Active, runner.reportStatus().state));

    const TestSupport::Types::Reporting::SinkStatus runnerFinalStatus = runner.finalize();
    static_cast<void>(context.expectEq("Runner finalization succeeds", TestSupport::Types::Reporting::SinkState::Finalized, runnerFinalStatus.state));
    static_cast<void>(context.expectTrue("Runner finalization has no failure", runnerFinalStatus.failureStatus.ok()));
    static_cast<void>(
        context.expectEq("Runner finalization is idempotent", TestSupport::Types::Reporting::SinkState::Finalized, runner.finalize().state));

    const TestSupport::Types::TextResult reportResult = TestSupport::readTextFile(root / "runner_report.txt");
    static_cast<void>(context.expectTrue("Runner report read succeeds", reportResult.status.ok()));
    static_cast<void>(context.expectContains("Runner report includes result", reportResult.text, "[RESULT] FirstSuite passed=1 failed=0 skipped=1"));
    static_cast<void>(context.expectContains("Section reports begin", reportResult.text, "begin section: small section"));
    static_cast<void>(context.expectContains("Section reports metric", reportResult.text, "section small section elapsedMs="));

    // Convert one suite exception into a failure and keep later suites runnable.
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
    static_cast<void>(context.expectEq(
        "Throwing runner finalizes independently",
        TestSupport::Types::Reporting::SinkState::Finalized,
        throwingRunner.finalize().state));
}
