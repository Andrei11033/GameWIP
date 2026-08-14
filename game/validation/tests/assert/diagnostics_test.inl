/// @file diagnostics_test.inl
/// @brief Private Assert correctness cases for diagnostic payloads, laziness, and UTF-8 truncation.

    /// @brief Verifies diagnostic text is present or intentionally stripped according to ASSERT_DIAGNOSTICS.
    void testDiagnosticConfiguration(TestContext &context)
    {
#if ASSERT_CHECKS_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "diagnostics"))
        {
            context.fail("diagnostics logger init", "Logger::init failed");
            return;
        }

        CHECK_MSG(false, "assert diagnostic message");
        Logger::flush(2s);
        const std::string contents = readFile(context, Logger::getLogFilePath());

#if ASSERT_DIAGNOSTICS
        context.expectTrue("diagnostics include condition", contents.find("false") != std::string::npos, "condition text missing");
        context.expectTrue("diagnostics include message", contents.find("assert diagnostic message") != std::string::npos, "custom message missing");
        context.expectTrue("diagnostics include location", contents.find("assert_test.cpp") != std::string::npos, "file text missing");
        context.expectTrue(
            "diagnostics include caller function",
            contents.find("testDiagnosticConfiguration") != std::string::npos,
            "function text missing");
#else
        context.expectTrue("diagnostics stripped condition", contents.find("false") == std::string::npos, "condition text was embedded");
        context.expectTrue(
            "diagnostics stripped message",
            contents.find("assert diagnostic message") == std::string::npos,
            "custom message was embedded");
        context.expectTrue("diagnostics stripped location", contents.find("assert_test.cpp") == std::string::npos, "location text was embedded");
#endif
#else
        context.pass("diagnostic logger test skipped because ASSERT_CHECKS_ENABLED=0");
#endif
    }

    /// @brief Verifies diagnostic messages are only evaluated when diagnostics are compiled in.
    void testDiagnosticMessageEvaluation(TestContext &context)
    {
#if ASSERT_CHECKS_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "diagnostic_message_evaluation"))
        {
            context.fail("diagnostic message evaluation logger init", "Logger::init failed");
            return;
        }

        int evaluations = 0;
        CHECK_MSG(false, makeDiagnosticMessage(evaluations));
        const std::string contents = readFile(context, Logger::getLogFilePath());

#if ASSERT_DIAGNOSTICS
        context.expectEq("diagnostic message evaluated when enabled", evaluations, 1);
        context.expectTrue(
            "diagnostic evaluated message logged",
            contents.find("evaluated diagnostic message") != std::string::npos,
            "evaluated message missing");
#else
        context.expectEq("diagnostic message skipped when stripped", evaluations, 0);
        context.expectTrue(
            "diagnostic evaluated message stripped",
            contents.find("evaluated diagnostic message") == std::string::npos,
            "diagnostic message was embedded");
#endif
#else
        context.pass("diagnostic message evaluation skipped because ASSERT_CHECKS_ENABLED=0");
#endif
    }

    /// @brief Verifies message expressions are skipped when macro families are compiled out.
    void testCompiledOutMessageEvaluation(TestContext &context)
    {
#if !ASSERT_ENABLED
        int assertMessageEvaluations = 0;
        ASSERT_MSG(false, makeDiagnosticMessage(assertMessageEvaluations));
        ASSERT_INTERACTIVE_MSG(false, makeDiagnosticMessage(assertMessageEvaluations));
        VERIFY_MSG(false, makeDiagnosticMessage(assertMessageEvaluations));
        VERIFY_INTERACTIVE_MSG(false, makeDiagnosticMessage(assertMessageEvaluations));
        context.expectEq("compiled-out assert messages skipped", assertMessageEvaluations, 0);
#else
        context.pass("compiled-out assert message test skipped because ASSERT_ENABLED=1");
#endif

#if !ASSERT_CHECKS_ENABLED
        int checkMessageEvaluations = 0;
        CHECK_MSG(false, makeDiagnosticMessage(checkMessageEvaluations));
        CHECK_ONCE_MSG(false, makeDiagnosticMessage(checkMessageEvaluations));
        (void)ENSURE_MSG(false, makeDiagnosticMessage(checkMessageEvaluations));
        context.expectEq("compiled-out check messages skipped", checkMessageEvaluations, 0);
#else
        context.pass("compiled-out check message test skipped because ASSERT_CHECKS_ENABLED=1");
#endif
    }

    /// @brief Verifies bounded diagnostic formatting never truncates inside a UTF-8 scalar.
    void testUtf8DiagnosticTruncation(TestContext &context)
    {
#if ASSERT_CHECKS_ENABLED && ASSERT_DIAGNOSTICS
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "utf8_truncation"))
        {
            context.fail("UTF-8 truncation logger init", "Logger::init failed");
            return;
        }

        std::string message;
        message.reserve(1'600);
        for (int index = 0; index < 400; ++index)
        {
            message += "\xF0\x9F\x98\x80";
        }

        CHECK_MSG(false, message);
        Logger::flush(2s);

        const TestSupport::Types::TextResult logText = TestSupport::readTextFile(Logger::getLogFilePath());
        const std::string statusText = TestSupport::formatInfrastructureStatus(logText.status);
        context.expectTrue("truncated diagnostic remains valid UTF-8", logText.status.ok(), statusText);
        if (!logText.status.ok())
        {
            return;
        }
        context.expectContains("truncated diagnostic has visible suffix", logText.text, "... [truncated]");
#else
        context.pass("UTF-8 diagnostic truncation skipped because checks or diagnostics are disabled");
#endif
    }
