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
            context.pass("manual assert UI tests skipped by AssertTestOptions");
            return;
        }

#if ASSERT_ENABLED
        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "manual_assert_ui"))
        {
            context.fail("manual assert UI logger init", "Logger::init failed");
            return;
        }

        const ScopedClearedEnvironmentVariable clearTestAction(testActionEnvironmentVariable);
        const ScopedClearedEnvironmentVariable clearSuppressPopup(suppressPopupEnvironmentVariable);
        if (!requireInfrastructure(context, "clear manual test action", clearTestAction.status()) ||
            !requireInfrastructure(context, "clear manual popup suppression", clearSuppressPopup.status()))
        {
            return;
        }

        context.emit("[MANUAL] Assert UI Ignore Once: click Ignore Once. The test should continue.\n");
        manualInteractiveIgnoreOnceSite();
        context.pass("manual assert UI Ignore Once continued");

        context.emit("[MANUAL] Assert UI Always Ignore: click Always Ignore. The second call should not show a dialog.\n");
        manualInteractiveAlwaysIgnoreSite();
        manualInteractiveAlwaysIgnoreSite();
        context.pass("manual assert UI Always Ignore suppressed second call");

#if defined(_WIN32)
        if (IsDebuggerPresent() != FALSE)
        {
            context.emit("[MANUAL] Assert UI Break: click Break, let the debugger stop, then continue execution.\n");
            manualInteractiveBreakSite();
            context.pass("manual assert UI Break continued after debugger resume");
        }
        else
        {
            context.pass("manual assert UI Break skipped because no debugger is attached");
        }
#else
        context.pass("manual assert UI Break skipped because this manual check is Windows-only");
#endif

        if (options.enableChildCrashTests)
        {
            const std::filesystem::path childLogDirectory = context.logRoot / "manual_interactive_abort_child";
            if (!requireInfrastructure(context, "create manual Abort child log directory", TestSupport::createDirectories(childLogDirectory)))
            {
                return;
            }
            const ScopedEnvironmentVariable childLogDirectoryOverride(childLogDirectoryEnvironmentVariable, pathText(childLogDirectory));
            const ScopedClearedEnvironmentVariable clearChildTestAction(testActionEnvironmentVariable);
            const ScopedClearedEnvironmentVariable clearChildSuppressPopup(suppressPopupEnvironmentVariable);
            if (!requireInfrastructure(context, "set manual Abort child log directory", childLogDirectoryOverride.status()) ||
                !requireInfrastructure(context, "clear manual Abort child action", clearChildTestAction.status()) ||
                !requireInfrastructure(context, "clear manual Abort child popup suppression", clearChildSuppressPopup.status()))
            {
                return;
            }

            context.emit("[MANUAL] Assert UI Abort: a child process dialog should appear. Click Abort; the parent should detect abnormal exit.\n");
            expectAbnormalChildExit(context, interactiveAbortChildArgument, "manual assert UI Abort child exits abnormally");

            const std::string childLogContents = readDirectoryFiles(context, childLogDirectory);
            context.expectTrue(
                "manual assert UI Abort child logs fatal",
                childLogContents.find("[FATAL][Assert]: Assert failed") != std::string::npos,
                "manual abort child fatal missing");
            context.expectTrue(
                "manual assert UI Abort child logs message",
                childLogContents.find("interactive abort child") != std::string::npos,
                "manual abort child message missing");
        }
        else
        {
            context.pass("manual assert UI Abort child skipped because child crash tests are disabled");
        }

        Logger::flush(2s);
#else
        context.pass("manual assert UI tests skipped because ASSERT_ENABLED=0");
#endif
    }
