/// @file report_modes_test.inl
/// @brief Private TestSupport report-mode tests.

/// ------------------------------------------------------------
/// Report modes
/// ------------------------------------------------------------

/// @brief Verifies append, truncate, flush, and console-verbosity report modes.
void testReportModes(TestSupport::Context &context, const std::filesystem::path &root)
{
    // Verify disabled and console-only sinks.
    const std::filesystem::path noReportPath = root / "no_report.txt";
    TestSupport::Types::Reporting::Options noReportOptions;
    noReportOptions.writeConsole = false;
    noReportOptions.writeReport = false;
    noReportOptions.reportPath = noReportPath;
    TestSupport::Context noReport("NoReport", noReportOptions);
    noReport.pass("not written");
    static_cast<void>(context.expectEq(
        "Report-disabled sink state is disabled",
        TestSupport::Types::Reporting::SinkState::Disabled,
        noReport.reportStatus().state));
    static_cast<void>(context.expectEq(
        "Report-disabled finalization remains disabled",
        TestSupport::Types::Reporting::SinkState::Disabled,
        noReport.finalize().state));
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
        const ScopedPromptStreams captured("");
        TestSupport::Context consoleOnly("ConsoleOnly", consoleOnlyOptions);
        consoleOnly.pass("console only line");
        consoleOnlyOutput = captured.output.str();
    }
    static_cast<void>(context.expectContains("Console-only report writes to stdout", consoleOnlyOutput, "console only line"));
    const TestSupport::Types::BoolResult consoleOnlyExists = TestSupport::fileExists(consoleOnlyPath);
    static_cast<void>(context.expectTrue("Console-only path inspection succeeds", consoleOnlyExists.status.ok()));
    static_cast<void>(context.expectFalse("Console-only report writes no file", consoleOnlyExists.value));

    // Verify custom console writers and their failure report.
    const std::filesystem::path customConsolePath = root / "custom_console_report.txt";
    TestSupport::Types::Reporting::Options customConsoleOptions = quietReport(customConsolePath);
    customConsoleOptions.writeConsole = true;
    customConsoleOptions.consoleWriter = &captureConsoleWriter;
    g_consoleWriterCategory.clear();
    g_consoleWriterLine.clear();
    {
        TestSupport::Context customConsole("ConsoleWriter", customConsoleOptions);
        customConsole.pass("custom output");
        static_cast<void>(customConsole.finalize());
    }
    static_cast<void>(context.expectEq("Custom console writer receives the report category", std::string("PASS"), g_consoleWriterCategory));
    static_cast<void>(context.expectEq(
        "Custom console writer receives the plain report line",
        std::string("[PASS] [ConsoleWriter] custom output"),
        g_consoleWriterLine));
    const TestSupport::Types::TextResult customConsoleReport = TestSupport::readTextFile(customConsolePath);
    static_cast<void>(context.expectTrue("Custom console report file read succeeds", customConsoleReport.status.ok()));
    static_cast<void>(context.expectContains("Report file remains unstyled", customConsoleReport.text, "[PASS] [ConsoleWriter] custom output"));
    static_cast<void>(context.expectFalse("Report file contains no terminal escape", customConsoleReport.text.contains('\x1b')));

    TestSupport::Types::Reporting::Options failedConsoleOptions;
    failedConsoleOptions.writeReport = false;
    failedConsoleOptions.consoleWriter = &failConsoleWriter;
    std::string failedConsoleOutput;
    std::string failedConsoleError;
    {
        const ScopedPromptStreams captured("");
        TestSupport::Context failedConsole("FailedConsoleWriter", failedConsoleOptions);
        failedConsole.pass("not duplicated");
        failedConsoleOutput = captured.output.str();
        failedConsoleError = captured.error.str();
    }
    static_cast<void>(context.expectFalse("Failed custom console writer does not duplicate output", failedConsoleOutput.contains("not duplicated")));
    static_cast<void>(context.expectContains("Failed custom console writer is diagnosed", failedConsoleError, "[TEST OUTPUT] console writer failed"));

    // Verify append, truncate, and destructor flush behavior.
    const std::filesystem::path appendPath = root / "append_report.txt";
    TestSupport::Types::Reporting::Options firstOptions = quietReport(appendPath);
    firstOptions.appendReport = false;
    TestSupport::Context first("AppendReport", firstOptions);
    first.pass("first line");
    static_cast<void>(
        context.expectEq("Append report first sink finalizes", TestSupport::Types::Reporting::SinkState::Finalized, first.finalize().state));

    TestSupport::Types::Reporting::Options secondOptions = quietReport(appendPath);
    secondOptions.appendReport = true;
    TestSupport::Context second("AppendReport", secondOptions);
    second.pass("second line");
    static_cast<void>(
        context.expectEq("Append report second sink finalizes", TestSupport::Types::Reporting::SinkState::Finalized, second.finalize().state));

    const TestSupport::Types::TextResult appendedResult = TestSupport::readTextFile(appendPath);
    static_cast<void>(context.expectTrue("Appended report read succeeds", appendedResult.status.ok()));
    const std::string &appended = appendedResult.text;
    static_cast<void>(context.expectContains("Report append preserves first line", appended, "first line"));
    static_cast<void>(context.expectContains("Report append writes second line", appended, "second line"));

    TestSupport::Types::Reporting::Options truncateOptions = quietReport(appendPath);
    truncateOptions.appendReport = false;
    TestSupport::Context truncated("AppendReport", truncateOptions);
    truncated.pass("third line");
    static_cast<void>(
        context.expectEq("Truncated report sink finalizes", TestSupport::Types::Reporting::SinkState::Finalized, truncated.finalize().state));

    const TestSupport::Types::TextResult truncatedResult = TestSupport::readTextFile(appendPath);
    static_cast<void>(context.expectTrue("Truncated report read succeeds", truncatedResult.status.ok()));
    const std::string &truncatedText = truncatedResult.text;
    static_cast<void>(context.expectContains("Report truncate writes new line", truncatedText, "third line"));
    static_cast<void>(context.expectFalse("Report truncate removes old line", truncatedText.contains("first line")));

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

    const std::filesystem::path invalidParent = root / "invalid_report_parent";
    static_cast<void>(context.expectTrue("Invalid report parent fixture writes", TestSupport::writeTextFile(invalidParent, "not a directory").ok()));
    const TestSupport::Types::Reporting::Options invalidReportOptions = quietReport(invalidParent / "child.txt");
    TestSupport::Context invalidReport("InvalidReport", invalidReportOptions);
    const TestSupport::Types::Reporting::SinkStatus invalidStatus = invalidReport.reportStatus();
    static_cast<void>(
        context.expectEq("Invalid report destination is failed", TestSupport::Types::Reporting::SinkState::Failed, invalidStatus.state));
    static_cast<void>(context.expectFalse("Invalid report destination exposes failure", invalidStatus.failureStatus.ok()));
    static_cast<void>(context.expectEq(
        "Invalid report finalization remains failed",
        TestSupport::Types::Reporting::SinkState::Failed,
        invalidReport.finalize().state));

    // Check which categories each console verbosity exposes.
    std::string conciseOutput;
    {
        const ScopedPromptStreams captured("");
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
    static_cast<void>(context.expectFalse("Concise console hides info", conciseOutput.contains("hidden info")));
    static_cast<void>(context.expectFalse("Concise console hides passes", conciseOutput.contains("hidden pass")));
    static_cast<void>(context.expectContains("Concise console includes failures", conciseOutput, "visible failure"));
    static_cast<void>(context.expectContains("Concise console includes skips", conciseOutput, "visible skip"));
    static_cast<void>(context.expectContains("Concise console includes summaries", conciseOutput, "visible summary"));

    std::string minimalOutput;
    {
        const ScopedPromptStreams captured("");
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
    static_cast<void>(context.expectFalse("Minimal console hides info", minimalOutput.contains("hidden minimal info")));
    static_cast<void>(context.expectFalse("Minimal console hides passes", minimalOutput.contains("hidden minimal pass")));
    static_cast<void>(context.expectFalse("Minimal console hides summaries", minimalOutput.contains("hidden minimal summary")));
    static_cast<void>(context.expectContains("Minimal console includes failures", minimalOutput, "visible minimal failure"));
    static_cast<void>(context.expectContains("Minimal console includes skips", minimalOutput, "visible minimal skip"));
    static_cast<void>(context.expectContains("Minimal console includes manual instructions", minimalOutput, "visible minimal instruction"));
}
