/// @file process_test.inl
/// @brief Private Assert correctness cases for isolated crash, break, and child-process behavior.

/// @brief Child-process body that intentionally triggers a failed ASSERT.
int runAssertFailureChild()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    Logger::Types::Config config;
    config.output = Logger::Types::OutputMode::None;
    config.minLevel = Logger::Types::Level::Trace;
    config.enableDebugOutput = false;
    config.enableFatalPopup = false;
    if (const char *childLogDirectory = std::getenv(std::string(childLogDirectoryEnvironmentVariable).c_str()))
    {
        config.output = Logger::Types::OutputMode::File;
        config.logDirectory = childLogDirectory;
        config.fallbackToConsoleOnFileFailure = false;
    }
    Logger::init(config);
    ASSERT_MSG(false, assertFailureChildMessage);
    Logger::shutdown();
    return 0;
}

/// @brief Executes the interactive Abort child protocol; correct behavior terminates the process.
int runInteractiveAbortChild()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    Logger::Types::Config config;
    config.output = Logger::Types::OutputMode::None;
    config.minLevel = Logger::Types::Level::Trace;
    config.enableDebugOutput = false;
    config.enableFatalPopup = false;
    if (const char *childLogDirectory = std::getenv(std::string(childLogDirectoryEnvironmentVariable).c_str()))
    {
        config.output = Logger::Types::OutputMode::File;
        config.logDirectory = childLogDirectory;
        config.fallbackToConsoleOnFileFailure = false;
    }
    Logger::init(config);
    ASSERT_INTERACTIVE_MSG(false, "interactive abort child");
    Logger::shutdown();
    return 0;
}

/// @brief Executes the interactive Break child protocol under deterministic debugger hooks.
int runInteractiveBreakChild()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    Logger::Types::Config config;
    config.output = Logger::Types::OutputMode::None;
    config.minLevel = Logger::Types::Level::Trace;
    config.enableDebugOutput = false;
    config.enableFatalPopup = false;
    if (const char *childLogDirectory = std::getenv(std::string(childLogDirectoryEnvironmentVariable).c_str()))
    {
        config.output = Logger::Types::OutputMode::File;
        config.logDirectory = childLogDirectory;
        config.fallbackToConsoleOnFileFailure = false;
    }
    Logger::init(config);
    ASSERT_INTERACTIVE_MSG(false, "interactive break child");
    Logger::shutdown();
    return 0;
}

/// @brief Child-process body that intentionally triggers DEBUG_BREAK.
int runDebugBreakChild()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    DEBUG_BREAK();
    return 0;
}

/// @brief Child-process body that intentionally triggers UNREACHABLE when assertions are enabled.
int runUnreachableChild()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    UNREACHABLE();
    return 0;
}

/// @brief Runs a child process and expects it to exit abnormally.
void expectAbnormalChildExit(TestContext &context, std::string_view argument, std::string_view testName)
{
    std::cout.flush();
    std::cerr.flush();
    const TestSupport::Types::Process::Result result = runChildProcessResult(context.executablePath, argument);
    context.expectTrue(
        testName,
        result.status.ok() && result.outcome == TestSupport::Types::Process::Outcome::Exited && result.exitCode != 0,
        "child process did not produce a normal nonzero exit");
}

/// @brief Verifies a failed ASSERT triggers an abnormal child-process exit when assertions are enabled.
void testAssertFailureChild(TestContext &context, const AssertTestOptions &options)
{
#if ASSERT_ENABLED
    if (!options.enableChildCrashTests)
    {
        context.pass("ASSERT failure child test disabled by AssertTestOptions");
        return;
    }

    const std::filesystem::path childLogDirectory = context.logRoot / "assert_child";
    if (!requireInfrastructure(context, "create ASSERT child log directory", TestSupport::createDirectories(childLogDirectory)))
    {
        return;
    }
    const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
    const ScopedEnvironmentVariable suppressPopupOverride(suppressPopupEnvironmentVariable, "1");
    if (!requireInfrastructure(context, "set ASSERT child log directory", childLogDirectoryOverride.status()) ||
        !requireInfrastructure(context, "suppress ASSERT child popup", suppressPopupOverride.status()))
    {
        return;
    }

    expectAbnormalChildExit(context, assertFailureChildArgument, "ASSERT failure child exits abnormally with popup suppressed");

    const std::string childLogContents = readDirectoryFiles(context, childLogDirectory);
    context.expectTrue(
        "ASSERT failure child logs fatal through Logger",
        childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos,
        "assert failure missing from child log");
#if ASSERT_DIAGNOSTICS
    context.expectTrue(
        "ASSERT failure child logs diagnostic message",
        childLogContents.find(assertFailureChildMessage) != std::string::npos,
        "child assert message missing from log");
#else
    context.expectTrue(
        "ASSERT failure child strips diagnostic message",
        childLogContents.find(assertFailureChildMessage) == std::string::npos,
        "child assert message was embedded");
#endif
#else
    context.pass("ASSERT failure child test skipped because ASSERT_ENABLED=0");
#endif
}

/// @brief Verifies interactive Abort reports before terminating its child process.
void testInteractiveAbortChild(TestContext &context, const AssertTestOptions &options)
{
#if ASSERT_ENABLED
    if (!options.enableChildCrashTests)
    {
        context.pass("ASSERT_INTERACTIVE abort child test disabled by AssertTestOptions");
        return;
    }

    const std::filesystem::path childLogDirectory = context.logRoot / "interactive_abort_child";
    if (!requireInfrastructure(context, "create interactive Abort child log directory", TestSupport::createDirectories(childLogDirectory)))
    {
        return;
    }
    const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
    const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "abort");
    if (!requireInfrastructure(context, "set interactive Abort child log directory", childLogDirectoryOverride.status()) ||
        !requireInfrastructure(context, "set interactive Abort action", testAction.status()))
    {
        return;
    }

    expectAbnormalChildExit(context, interactiveAbortChildArgument, "ASSERT_INTERACTIVE abort child exits abnormally");

    const std::string childLogContents = readDirectoryFiles(context, childLogDirectory);
    context.expectTrue(
        "ASSERT_INTERACTIVE abort child logs fatal",
        childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos,
        "interactive abort child fatal missing");
#if ASSERT_DIAGNOSTICS
    context.expectTrue(
        "ASSERT_INTERACTIVE abort child logs message",
        childLogContents.find("interactive abort child") != std::string::npos,
        "interactive abort child message missing");
#else
    context.expectTrue(
        "ASSERT_INTERACTIVE abort child strips message",
        childLogContents.find("interactive abort child") == std::string::npos,
        "interactive abort child message was embedded");
#endif
#else
    context.pass("ASSERT_INTERACTIVE abort child skipped because ASSERT_ENABLED=0");
#endif
}

/// @brief Verifies interactive Break child handling without requiring a real debugger.
void testInteractiveBreakChild(TestContext &context, const AssertTestOptions &options)
{
#if ASSERT_ENABLED
    if (!options.enableChildCrashTests)
    {
        context.pass("ASSERT_INTERACTIVE break child test disabled by AssertTestOptions");
        return;
    }

    const std::filesystem::path childLogDirectory = context.logRoot / "interactive_break_child";
    if (!requireInfrastructure(context, "create interactive Break child log directory", TestSupport::createDirectories(childLogDirectory)))
    {
        return;
    }
    const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
    const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "break");
    if (!requireInfrastructure(context, "set interactive Break child log directory", childLogDirectoryOverride.status()) ||
        !requireInfrastructure(context, "set interactive Break action", testAction.status()))
    {
        return;
    }

    expectAbnormalChildExit(context, interactiveBreakChildArgument, "ASSERT_INTERACTIVE break child exits abnormally without debugger");

    const std::string childLogContents = readDirectoryFiles(context, childLogDirectory);
    context.expectTrue(
        "ASSERT_INTERACTIVE break child logs fatal",
        childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos,
        "interactive break child fatal missing");
#if ASSERT_DIAGNOSTICS
    context.expectTrue(
        "ASSERT_INTERACTIVE break child logs message",
        childLogContents.find("interactive break child") != std::string::npos,
        "interactive break child message missing");
#else
    context.expectTrue(
        "ASSERT_INTERACTIVE break child strips message",
        childLogContents.find("interactive break child") == std::string::npos,
        "interactive break child message was embedded");
#endif
#else
    context.pass("ASSERT_INTERACTIVE break child skipped because ASSERT_ENABLED=0");
#endif
}

/// @brief Verifies DEBUG_BREAK exits abnormally in an isolated child process.
void testDebugBreakChild(TestContext &context)
{
    expectAbnormalChildExit(context, debugBreakChildArgument, "DEBUG_BREAK child exits abnormally");
}

/// @brief Verifies UNREACHABLE exits abnormally unless disabled unreachable is configured as an optimizer assumption.
void testUnreachableChild(TestContext &context)
{
#if ASSERT_ENABLED || !ASSERT_UNREACHABLE_ASSUME
    const ScopedEnvironmentVariable suppressPopupOverride(suppressPopupEnvironmentVariable, "1");
    if (!requireInfrastructure(context, "suppress UNREACHABLE child popup", suppressPopupOverride.status()))
    {
        return;
    }
    expectAbnormalChildExit(context, unreachableChildArgument, "UNREACHABLE child exits abnormally with popup suppressed");
#else
    context.pass("UNREACHABLE child test skipped because ASSERT_UNREACHABLE_ASSUME=1");
#endif
}
