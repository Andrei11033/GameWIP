/// @file manual_test.inl
/// @brief Private Assert correctness cases for opt-in real interactive UI validation.

/// @brief Provides the stable call site used by the human Ignore Once dialog check.
void manualInteractiveIgnoreOnceSite()
{
    ASSERT_INTERACTIVE_MSG(false, "manual assert UI Ignore Once test - click Ignore Once to continue");
}

/// @brief Provides the stable call site used by the human Always Ignore dialog check.
void manualInteractiveAlwaysIgnoreSite()
{
    ASSERT_INTERACTIVE_MSG(false, "manual assert UI Always Ignore test - click Always Ignore to suppress the second call");
}

/// @brief Provides the stable call site used by the human debugger Break check.
void manualInteractiveBreakSite()
{
    ASSERT_INTERACTIVE_MSG(false, "manual assert UI Break test - click Break while a debugger is attached, then continue execution");
}

/// @brief Runs opt-in human checks for real action dialogs, fallback UI, and debugger breaks.
void testManualAssertUi(TestContext &context, const AssertTestOptions &options)
{
    if (!options.enableManualTests)
    {
        context.skip("manual assert UI tests", "disabled by AssertTestOptions");
        return;
    }

#if ASSERT_ENABLED
    ScopedLoggerShutdown loggerShutdown;

    if (!initFileLogger(context, "manual_assert_ui"))
    {
        context.fail("manual assert UI logger init", "Logger::init failed");
        return;
    }

    const ScopedClearedEnvironmentVariable clearTestAction(context.processState, kTestActionEnvironmentVariable);
    const ScopedClearedEnvironmentVariable clearSuppressPopup(context.processState, kSuppressPopupEnvironmentVariable);

    if (!requireInfrastructure(context, "clear manual test action", clearTestAction.status()) ||
        !requireInfrastructure(context, "clear manual popup suppression", clearSuppressPopup.status()))
    {
        return;
    }

    context.emit("[MANUAL] Assert UI Ignore Once: click Ignore Once. The test should continue.\n");
    manualInteractiveIgnoreOnceSite();
    context.manualCheck("manual assert UI Ignore Once", "Did the assertion dialog appear, and did Ignore Once let execution continue?");

    context.emit("[MANUAL] Assert UI Always Ignore: click Always Ignore. The second call should not show a dialog.\n");
    manualInteractiveAlwaysIgnoreSite();
    manualInteractiveAlwaysIgnoreSite();
    context.manualCheck("manual assert UI Always Ignore", "Did Always Ignore dismiss the first dialog and suppress the second call's dialog?");

#if defined(_WIN32)
    if (IsDebuggerPresent() != FALSE)
    {
        context.emit("[MANUAL] Assert UI Break: click Break, let the debugger stop, then continue execution.\n");
        manualInteractiveBreakSite();
        context.manualCheck("manual assert UI Break", "Did Break stop in the debugger, and could you resume execution?");
    }
    else
    {
        context.skip("manual assert UI Break", "no debugger is attached");
    }
#else
    context.skip("manual assert UI Break", "this manual check is Windows-only");
#endif

    if (options.enableChildCrashTests)
    {
        const std::filesystem::path childLogDirectory = context.logRoot / "manual_interactive_abort_child";

        if (!requireInfrastructure(context, "create manual Abort child log directory", TestSupport::createDirectories(childLogDirectory)))
        {
            return;
        }
        const ScopedEnvironmentVariable childLogDirectoryOverride(
            context.processState,
            kChildLogDirectoryEnvironmentVariable,
            pathText(childLogDirectory));
        const ScopedClearedEnvironmentVariable clearChildTestAction(context.processState, kTestActionEnvironmentVariable);
        const ScopedClearedEnvironmentVariable clearChildSuppressPopup(context.processState, kSuppressPopupEnvironmentVariable);

        if (!requireInfrastructure(context, "set manual Abort child log directory", childLogDirectoryOverride.status()) ||
            !requireInfrastructure(context, "clear manual Abort child action", clearChildTestAction.status()) ||
            !requireInfrastructure(context, "clear manual Abort child popup suppression", clearChildSuppressPopup.status()))
        {
            return;
        }

        context.emit("[MANUAL] Assert UI Abort: a child process dialog should appear. Click Abort; the parent should detect abnormal exit.\n");
        // Human response time is separate from the short automated child budget.
        expectAbnormalChildExit(context, kInteractiveAbortChildArgument, "manual assert UI Abort child exits abnormally", 5min);
        context.manualCheck("manual assert UI Abort", "Did the child assertion dialog appear, and did Abort close the child?");

        const std::string childLogContents = readDirectoryFiles(context, childLogDirectory);
        context.expectTrue(
            "manual assert UI Abort child logs fatal",
            childLogContents.contains("[FATAL][Assert]: Assert failed"),
            "manual abort child fatal missing");
        context.expectTrue(
            "manual assert UI Abort child logs message",
            childLogContents.contains("interactive abort child"),
            "manual abort child message missing");
    }
    else
    {
        context.skip("manual assert UI Abort child", "child crash tests are disabled");
    }

    Logger::flush(2s);
#else
    context.skip("manual assert UI tests", "ASSERT_ENABLED=0");
#endif
}
