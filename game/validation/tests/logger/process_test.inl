/// @file process_test.inl
/// @brief Logger child-process and manual diagnostic correctness suites.

[[nodiscard]] TestSupport::Types::Process::Result runChild(
    std::string_view executable,
    std::string_view argument,
    std::chrono::milliseconds timeout = 5s)
{
    TestSupport::Types::Process::Options options;
    options.executablePath = std::filesystem::path(std::string(executable));
    options.arguments = {std::string(argument)};
    options.timeout = timeout;
    options.captureOutput = true;
    return TestSupport::runChildProcess(options);
}

[[nodiscard]] bool hasArgument(int argc, char **argv, std::string_view argument)
{
    const auto arguments = GameWIP::Validation::processArguments(argc, argv);
    for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
    {
        if (value != nullptr && std::string_view(value) == argument)
            return true;
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
    if (const char *directory = std::getenv(std::string(childLogDirectoryEnvironmentVariable).c_str()))
    {
        config.output = Logger::Types::OutputMode::File;
        config.logDirectory = directory;
        config.fallbackToConsoleOnFileFailure = false;
        config.flushFileEveryBatch = true;
    }
    static_cast<void>(Logger::init(config));
    Logger::fatalTerminate(testSource, fatalTerminateChildMessage);
}

void testFatalTerminateChild(TestContext &context, const LoggerTestOptions &options)
{
    if (!options.enableChildCrashTests)
    {
        context.pass("fatalTerminate child disabled by LoggerTestOptions");
        return;
    }

    const std::filesystem::path directory = testDirectory(context, "fatal-terminate-child");
    const TestSupport::ScopedEnvironmentVariable environment(childLogDirectoryEnvironmentVariable, pathText(directory));
    if (!environment.status().ok())
    {
        context.fail("set fatalTerminate child directory", TestSupport::formatInfrastructureStatus(environment.status()));
        return;
    }

    const TestSupport::Types::Process::Result child = runChild(context.executablePath, fatalTerminateChildArgument);
    context.expectTrue(
        "fatalTerminate child exits nonzero",
        child.status.ok() && child.outcome == TestSupport::Types::Process::Outcome::Exited && child.exitCode != 0);

    std::string contents;
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
            contents += readWholeFile(context, entry.path());
    }
    context.expectContains("fatalTerminate uses synchronous report", contents, fatalTerminateChildMessage);
}

void testManualFatalPopup(TestContext &context, const LoggerTestOptions &options)
{
    if (!options.enableManualTests)
    {
        context.pass("manual Logger fatal popup skipped by LoggerTestOptions");
        return;
    }

    ScopedLoggerShutdown shutdown;
    Logger::Types::Config config = makeConsoleConfig();
    config.enableFatalPopup = true;
    expectStarted(context, "manual fatal popup init", Logger::init(config));
    static_cast<void>(Logger::reportFatal(testSource, "Manual Logger fatal popup test. Close this popup to continue."));
    context.pass("manual Logger fatal popup completed");
}
