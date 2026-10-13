/// @file process_test.inl
/// @brief Logger child-process and manual diagnostic correctness suites.

[[nodiscard]] TestSupport::Types::Process::Result runChild(
    std::string_view executable,
    std::string_view argument,
    // Crash logging may cross the Windows error-reporting boundary before the child exits.
    std::chrono::milliseconds timeout = 30s)
{
    TestSupport::Types::Process::Options options;
    options.executablePath = std::filesystem::path(std::string(executable));
    options.arguments = {std::string(argument)};
    options.waitTimeout = timeout;
    options.captureOutput = true;
    return TestSupport::runChildProcess(options);
}

[[nodiscard]] bool hasArgument(int argc, char **argv, std::string_view argument)
{
    const auto arguments = GameWIP::Validation::processArguments(argc, argv);
    for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
    {
        if (value != nullptr && std::string_view(value) == argument)
        {
            return true;
        }
    }
    return false;
}

int runFatalTerminateChild()
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    Logger::Types::Config config;
    config.output = Logger::Types::OutputMode::None;
    config.enableDebugOutput = false;
    config.enableFatalPopup = false;
    if (const char *directory = std::getenv(std::string(kChildLogDirectoryEnvironmentVariable).c_str()))
    {
        config.output = Logger::Types::OutputMode::File;
        config.logDirectory = directory;
        config.fallbackToConsoleOnFileFailure = false;
        config.flushFileEveryBatch = true;
    }
    static_cast<void>(Logger::init(config));
    Logger::fatalTerminate(kTestSource, kFatalTerminateChildMessage);
}

void testFatalTerminateChild(TestContext &context, const LoggerTestOptions &options)
{
    if (!options.enableChildCrashTests)
    {
        context.skip("fatalTerminate child", "disabled by LoggerTestOptions");
        return;
    }

    const std::filesystem::path directory = testDirectory(context, "fatal-terminate-child");
    const TestSupport::ScopedEnvironmentVariable environment(context.processState, kChildLogDirectoryEnvironmentVariable, pathText(directory));
    if (!environment.status().ok())
    {
        context.fail("set fatalTerminate child directory", TestSupport::formatInfrastructureStatus(environment.status()));
        return;
    }

    const TestSupport::Types::Process::Result child = runChild(context.executablePath, kFatalTerminateChildArgument);
    context.expectTrue(
        "fatalTerminate child exits nonzero",
        child.status.ok() && child.outcome == TestSupport::Types::Process::Outcome::Exited && child.exitCode != 0);

    std::string contents;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            contents += readWholeFile(context, entry.path());
        }
    }
    context.expectContains("fatalTerminate uses synchronous report", contents, kFatalTerminateChildMessage);
}

void testManualFatalPopup(TestContext &context, const LoggerTestOptions &options)
{
    if (!options.enableManualTests)
    {
        context.skip("manual Logger fatal popup", "disabled by LoggerTestOptions");
        return;
    }

    ScopedLoggerShutdown shutdown;
    Logger::Types::Config config = makeConsoleConfig();
    config.enableFatalPopup = true;
    const auto initialized = Logger::init(config);
    expectStarted(context, "manual fatal popup init", initialized);
    if (!initialized.status.ok() || initialized.outcome != Logger::Types::Init::Outcome::Started)
    {
        return;
    }
    const auto report = Logger::reportFatal(kTestSource, "Manual Logger fatal popup test. Close this popup to continue.");
    context.expectTrue(
        "manual fatal popup report delivered",
        reportDelivered(report),
        std::format("code={} native={} message={}", IO::errorCodeName(report.status.code), report.status.nativeCode, report.status.message));
    if (!reportDelivered(report))
    {
        return;
    }
    std::string skipReason;
    const auto answer =
        TestSupport::promptManualCheck("Did the fatal popup show the Manual Logger message, and could you close it normally?", skipReason);
    switch (answer)
    {
    case TestSupport::Types::Reporting::ManualAnswer::Yes:
        ++context.manualObservations.passed;
        context.pass("manual Logger fatal popup observation");
        break;
    case TestSupport::Types::Reporting::ManualAnswer::No:
        ++context.manualObservations.failed;
        context.fail("manual Logger fatal popup observation", "manual observation rejected by user");
        break;
    case TestSupport::Types::Reporting::ManualAnswer::Skipped:
        ++context.manualObservations.skipped;
        context.skip("manual Logger fatal popup observation", skipReason);
        break;
    }
}
