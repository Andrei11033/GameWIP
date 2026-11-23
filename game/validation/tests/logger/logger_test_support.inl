/// @file logger_test_support.inl
/// @brief Logger test context, fixtures, and shared helpers.

/// ------------------------------------------------------------
/// Logger test context
/// ------------------------------------------------------------

struct TestContext
{
    explicit TestContext(TestSupport::Context &context) noexcept
        : testContext(context)
    {
    }

    // Runner and process state

    TestSupport::Context &testContext;
    TestSupport::ScopedProcessState processState;

    // Suite workspace

    std::filesystem::path logRoot;
    std::filesystem::path executablePath;

    // Manual observations

    TestSupport::Types::Reporting::Summary manualObservations;

    // Runner reporting

    void pass(std::string_view name)
    {
        testContext.pass(name);
    }

    void skip(std::string_view name, std::string_view reason)
    {
        testContext.skip(name, reason);
    }

    void fail(std::string_view name, std::string_view details)
    {
        testContext.fail(name, details);
    }

    // Test assertions

    void expectTrue(std::string_view name, bool value, std::string_view details = "expected true")
    {
        if(value)
        {
            testContext.pass(name);
        }
        else
        {
            testContext.fail(name, details);
        }
    }

    void expectFalse(std::string_view name, bool value, std::string_view details = "expected false")
    {
        expectTrue(name, !value, details);
    }

    template <typename Left, typename Right> void expectEq(std::string_view name, const Left &actual, const Right &expected)
    {
        static_cast<void>(testContext.expectEq(name, expected, actual));
    }

    void expectContains(std::string_view name, std::string_view text, std::string_view needle)
    {
        static_cast<void>(testContext.expectContains(name, text, needle));
    }
};

/// ------------------------------------------------------------
/// Test helpers and fixtures
/// ------------------------------------------------------------

// Test case lifetime

template <typename Function> void runCase(TestContext &context, std::string_view name, Function &&function)
{
    const TestSupport::Section section(context.testContext, name);
    try
    {
        std::forward<Function>(function)();
    }
    catch(const std::exception &exception)
    {
        static_cast<void>(Logger::shutdown());
        context.fail(name, exception.what());
    }
    catch(...)
    {
        static_cast<void>(Logger::shutdown());
        context.fail(name, "unknown exception");
    }
}

struct ScopedLoggerShutdown
{
    ~ScopedLoggerShutdown()
    {
        static_cast<void>(Logger::shutdown());
    }
};

// Result and path helpers

[[nodiscard]] bool flushCompleted(const Logger::Types::FlushResult &result) noexcept
{
    return result.status.ok() && result.outcome == Logger::Types::FlushOutcome::Completed;
}

[[nodiscard]] bool reportDelivered(const Logger::Types::Report::Result &result) noexcept
{
    return result.status.ok() && result.outcome == Logger::Types::Report::Outcome::Completed &&
           result.delivery != Logger::Types::Report::Delivery::None;
}

[[nodiscard]] std::string pathText(TestContext &context, const std::filesystem::path &path)
{
    return GameWIP::Test::Internal::pathToUtf8(context, path, "convert Logger test path");
}

[[nodiscard]] std::filesystem::path pathFromText(TestContext &context, std::string_view text)
{
    return GameWIP::Test::Internal::pathFromUtf8(context, text, "convert Logger path from UTF-8");
}

[[nodiscard]] std::filesystem::path loggerLogFilePath(TestContext &context)
{
    const Logger::Types::LogFilePathResult result = Logger::getLogFilePath();

    if(!result.status.ok())
    {
        context.fail("query Logger log file path", std::format("code={} native={}", static_cast<int>(result.status.code), result.status.nativeCode));
        return {};
    }

    return pathFromText(context, result.utf8);
}

[[nodiscard]] std::string readWholeFile(TestContext &context, const std::filesystem::path &path)
{
    TestSupport::Types::TextResult result = TestSupport::readTextFile(path);

    if(!result.status.ok())
    {
        context.fail("read Logger log fixture", TestSupport::formatInfrastructureStatus(result.status));
        return {};
    }

    return std::move(result.text);
}

[[nodiscard]] std::filesystem::path testDirectory(TestContext &context, std::string_view name)
{
    std::filesystem::path directory = context.logRoot / std::string(name);
    const auto status = TestSupport::createDirectories(directory);

    if(!status.ok())
    {
        context.fail("create Logger test directory", TestSupport::formatInfrastructureStatus(status));
    }

    return directory;
}

// Logger configuration fixtures

struct OwnedLoggerConfig : Logger::Types::Config
{
    std::string ownedDirectory;

    const Logger::Types::Config &ready()
    {
        logDirectory = ownedDirectory;
        return *this;
    }
};

[[nodiscard]] Logger::Types::Config makeConsoleConfig(Logger::Types::Level minLevel = Logger::Types::Level::Trace)
{
    Logger::Types::Config config;
    config.output = Logger::Types::OutputMode::Console;
    config.minLevel = minLevel;
    config.maxQueueSize = 256;
    config.maxMessageLength = 512;
    config.inlineMessageCapacity = 128;
    config.workerBatchSize = 64;
    config.enableConsoleColor = false;
    config.enableDebugOutput = false;
    config.enableFatalPopup = false;
    config.releaseMessageMemoryAfterWrite = true;
    config.releaseStorageOnShutdown = true;
    return config;
}

[[nodiscard]] OwnedLoggerConfig makeFileConfig(
    TestContext &context,
    std::string_view name,
    Logger::Types::Level minLevel = Logger::Types::Level::Trace)
{
    OwnedLoggerConfig config;
    static_cast<Logger::Types::Config &>(config) = makeConsoleConfig(minLevel);
    config.output = Logger::Types::OutputMode::File;
    config.fallbackToConsoleOnFileFailure = false;
    config.ownedDirectory = pathText(context, testDirectory(context, name));
    if(config.ownedDirectory.empty())
    {
        return config;
    }

    config.logDirectory = config.ownedDirectory;
    return config;
}
