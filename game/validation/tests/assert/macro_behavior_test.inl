/// @file macro_behavior_test.inl
/// @brief Private Assert correctness cases for public macro evaluation and reporting behavior.

/// @brief Exercises passing macros and statement safety.
void testPassingMacros(TestContext &context)
{
    int value = 0;

    if (true) // NOLINT(readability-simplify-boolean-expr) -- Verifies macro statement safety in a conditional.
        ASSERT(true);
    else
        ++value;

    ASSERT_MSG(value == 0, "passing assert message");
    CHECK(true);
    CHECK_MSG(value == 0, "passing check message");
    CHECK_ONCE(true);
    CHECK_ONCE_MSG(true, "passing check once message");

    context.expectEq("passing macros keep value", value, 0);
}

/// @brief Verifies disabled reporting macros do not evaluate expressions with side effects.
void testDisabledMacroEvaluation(TestContext &context)
{
    int assertEvaluations = 0;
    ASSERT(++assertEvaluations == 1);
    ASSERT_MSG(++assertEvaluations == 2, "disabled assert message");

#if ASSERT_ENABLED
    context.expectEq("ASSERT macros evaluate when enabled", assertEvaluations, 2);
#else
    context.expectEq("ASSERT macros skip expressions when disabled", assertEvaluations, 0);
#endif

    int checkEvaluations = 0;
    CHECK(++checkEvaluations == 1);
    CHECK_MSG(++checkEvaluations == 2, "disabled check message");
    CHECK_ONCE(++checkEvaluations == 3);
    CHECK_ONCE_MSG(++checkEvaluations == 4, "disabled check once message");

#if ASSERT_CHECKS_ENABLED
    context.expectEq("CHECK macros evaluate when enabled", checkEvaluations, 4);
#else
    context.expectEq("CHECK macros skip expressions when disabled", checkEvaluations, 0);
#endif
}

/// @brief Verifies that VERIFY evaluates its expression even when assertion reporting is disabled.
void testVerifyEvaluation(TestContext &context)
{
    int evaluations = 0;
    VERIFY(++evaluations == 1);
    VERIFY_MSG(++evaluations == 2, "verify message");
    context.expectEq("VERIFY evaluates expressions", evaluations, 2);
}

/// @brief Verifies that ENSURE evaluates once and returns the condition result.
void testEnsureBehavior(TestContext &context)
{
    ScopedLoggerShutdown loggerShutdown;
    initFileLogger(context, "ensure");

    int evaluations = 0;
    const bool first = ENSURE(++evaluations == 1);
    const bool second = ENSURE_MSG(++evaluations == 3, "ensure false message");

    context.expectTrue("ENSURE true result", first);
    context.expectTrue("ENSURE false result", !second, "expected false");
    context.expectEq("ENSURE evaluates once per call", evaluations, 2);

#if ASSERT_CHECKS_ENABLED
    const std::string contents = readFile(context, Logger::getLogFilePath());
#if ASSERT_DIAGNOSTICS
    context.expectTrue(
        "ENSURE diagnostics include caller function",
        contents.find("testEnsureBehavior") != std::string::npos,
        "caller function missing");
    context.expectTrue(
        "ENSURE diagnostics avoid lambda function",
        contents.find("operator()") == std::string::npos,
        "lambda function leaked into diagnostics");
#else
    context.expectTrue(
        "ENSURE diagnostics stripped message",
        contents.find("ensure false message") == std::string::npos,
        "diagnostic message was embedded");
#endif
#endif
}

/// @brief Verifies that CHECK_ONCE reports only one flushed log entry per call site.
void testCheckOnceLogging(TestContext &context)
{
#if ASSERT_CHECKS_ENABLED
    ScopedLoggerShutdown loggerShutdown;
    if (!initFileLogger(context, "check_once"))
    {
        context.fail("CHECK_ONCE logger init", "Logger::init failed");
        return;
    }

    Logger::resetStats();
    for (int index = 0; index < 3; ++index)
    {
        CHECK_ONCE(false);
    }
    Logger::flush(2s);
    const Logger::Types::Stats stats = Logger::getStats();
    const std::string contents = readFile(context, Logger::getLogFilePath());
    context.expectEq("CHECK_ONCE reports without queueing", stats.queued, std::size_t{0});
    context.expectEq("CHECK_ONCE writes one failure synchronously", stats.written, std::size_t{1});
    context.expectTrue(
        "CHECK_ONCE log contains error failure",
        contents.find("[ERROR][Check]: Check failed") != std::string::npos,
        "check failure missing from log");
#else
    context.pass("CHECK_ONCE logger test skipped because ASSERT_CHECKS_ENABLED=0");
#endif
}
