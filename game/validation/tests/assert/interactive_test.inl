/// @file interactive_test.inl
/// @brief Private Assert correctness cases for interactive failure actions and call-site suppression.

/// @brief Provides one stable macro call site for automated Always Ignore behavior.
void interactiveAlwaysIgnoreSite()
{
    ASSERT_INTERACTIVE_MSG(false, "interactive always ignore test");
}

/// @brief Provides one stable macro call site for automated Ignore Once behavior.
void interactiveIgnoreOnceSite()
{
    ASSERT_INTERACTIVE_MSG(false, "interactive ignore once repeat test");
}

/// @brief Provides a stable VERIFY_INTERACTIVE site while tracking expression evaluation.
void verifyInteractiveAlwaysIgnoreSite(int &evaluations)
{
    VERIFY_INTERACTIVE_MSG(++evaluations < 0, "verify interactive always ignore test");
}

/// @brief Verifies Ignore Once reports each invocation without suppressing the call site.
void testInteractiveIgnoreOnce(TestContext &context)
{
#if ASSERT_ENABLED
    ScopedLoggerShutdown loggerShutdown;
    if (!initFileLogger(context, "interactive_ignore_once"))
    {
        context.fail("interactive ignore once logger init", "Logger::init failed");
        return;
    }

    const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "ignore_once");
    if (!requireInfrastructure(context, "set interactive ignore-once action", testAction.status()))
    {
        return;
    }
    ASSERT_INTERACTIVE_MSG(false, "interactive ignore once test");

    Logger::flush(2s);
    const Logger::Types::Stats stats = Logger::getStats();
    const std::string contents = readFile(context, Logger::getLogFilePath());
    context.expectEq("ASSERT_INTERACTIVE ignore_once not queued", stats.queued, std::size_t{0});
    context.expectEq("ASSERT_INTERACTIVE ignore_once writes one fatal", stats.written, std::size_t{1});
    context.expectTrue(
        "ASSERT_INTERACTIVE ignore_once logs fatal",
        contents.find("[FATAL][Assert]: Assert failed") != std::string::npos,
        "interactive fatal missing");
#if ASSERT_DIAGNOSTICS
    context.expectTrue(
        "ASSERT_INTERACTIVE ignore_once logs message",
        contents.find("interactive ignore once test") != std::string::npos,
        "interactive message missing");
#else
    context.expectTrue(
        "ASSERT_INTERACTIVE ignore_once strips message",
        contents.find("interactive ignore once test") == std::string::npos,
        "interactive message was embedded");
#endif
#else
    context.pass("ASSERT_INTERACTIVE ignore_once skipped because ASSERT_ENABLED=0");
#endif
}

/// @brief Verifies Always Ignore suppresses later failures only at the same macro call site.
void testInteractiveAlwaysIgnore(TestContext &context)
{
#if ASSERT_ENABLED
    ScopedLoggerShutdown loggerShutdown;
    if (!initFileLogger(context, "interactive_always_ignore"))
    {
        context.fail("interactive always ignore logger init", "Logger::init failed");
        return;
    }

    const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "always_ignore");
    if (!requireInfrastructure(context, "set interactive always-ignore action", testAction.status()))
    {
        return;
    }
    interactiveAlwaysIgnoreSite();
    interactiveAlwaysIgnoreSite();

    Logger::flush(2s);
    const Logger::Types::Stats stats = Logger::getStats();
    const std::string contents = readFile(context, Logger::getLogFilePath());
    context.expectEq("ASSERT_INTERACTIVE always_ignore not queued", stats.queued, std::size_t{0});
    context.expectEq("ASSERT_INTERACTIVE always_ignore writes once", stats.written, std::size_t{1});
#if ASSERT_DIAGNOSTICS
    context.expectEq("ASSERT_INTERACTIVE always_ignore one message", countOccurrences(contents, "interactive always ignore test"), std::size_t{1});
#else
    context.expectEq("ASSERT_INTERACTIVE always_ignore strips message", countOccurrences(contents, "interactive always ignore test"), std::size_t{0});
#endif
#else
    context.pass("ASSERT_INTERACTIVE always_ignore skipped because ASSERT_ENABLED=0");
#endif
}

/// @brief Verifies interactive VERIFY evaluates its expression exactly once per invocation.
void testVerifyInteractiveEvaluation(TestContext &context)
{
    ScopedLoggerShutdown loggerShutdown;
    if (!initFileLogger(context, "verify_interactive"))
    {
        context.fail("verify interactive logger init", "Logger::init failed");
        return;
    }

    int passingEvaluations = 0;
    VERIFY_INTERACTIVE(++passingEvaluations == 1);
    context.expectEq("VERIFY_INTERACTIVE passing evaluates once", passingEvaluations, 1);

    int failingEvaluations = 0;
    const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "ignore_once");
    if (!requireInfrastructure(context, "set VERIFY_INTERACTIVE action", testAction.status()))
    {
        return;
    }
    VERIFY_INTERACTIVE_MSG(++failingEvaluations < 0, "verify interactive ignore once test");
    context.expectEq("VERIFY_INTERACTIVE failing evaluates once", failingEvaluations, 1);

#if ASSERT_ENABLED
    Logger::flush(2s);
    const std::string contents = readFile(context, Logger::getLogFilePath());
#if ASSERT_DIAGNOSTICS
    context.expectTrue(
        "VERIFY_INTERACTIVE failure logs when enabled",
        contents.find("verify interactive ignore once test") != std::string::npos,
        "verify interactive message missing");
#else
    context.expectTrue(
        "VERIFY_INTERACTIVE failure strips message",
        contents.find("verify interactive ignore once test") == std::string::npos,
        "verify interactive message was embedded");
#endif
#else
    Logger::flush(2s);
    const Logger::Types::Stats stats = Logger::getStats();
    context.expectEq("VERIFY_INTERACTIVE disabled does not report", stats.written, std::size_t{0});
#endif
}

/// @brief Verifies Always Ignore suppresses diagnostics without suppressing VERIFY evaluation.
void testVerifyInteractiveAlwaysIgnoreStillEvaluates(TestContext &context)
{
#if ASSERT_ENABLED
    ScopedLoggerShutdown loggerShutdown;
    if (!initFileLogger(context, "verify_interactive_always_ignore"))
    {
        context.fail("verify interactive always ignore logger init", "Logger::init failed");
        return;
    }

    int evaluations = 0;
    const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "always_ignore");
    if (!requireInfrastructure(context, "set VERIFY_INTERACTIVE always-ignore action", testAction.status()))
    {
        return;
    }
    verifyInteractiveAlwaysIgnoreSite(evaluations);
    verifyInteractiveAlwaysIgnoreSite(evaluations);

    Logger::flush(2s);
    const std::string contents = readFile(context, Logger::getLogFilePath());
    context.expectEq("VERIFY_INTERACTIVE Always Ignore still evaluates", evaluations, 2);
#if ASSERT_DIAGNOSTICS
    context.expectEq(
        "VERIFY_INTERACTIVE Always Ignore logs once",
        countOccurrences(contents, "verify interactive always ignore test"),
        std::size_t{1});
#else
    context.expectEq(
        "VERIFY_INTERACTIVE Always Ignore strips message",
        countOccurrences(contents, "verify interactive always ignore test"),
        std::size_t{0});
#endif
#else
    context.pass("VERIFY_INTERACTIVE Always Ignore test skipped because ASSERT_ENABLED=0");
#endif
}
