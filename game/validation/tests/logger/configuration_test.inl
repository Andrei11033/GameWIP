/// @file configuration_test.inl
/// @brief Logger initialization, configuration, and file-output correctness suites.

void expectStarted(TestContext &context, std::string_view name, const Logger::Types::Init::Result &result)
{
    context.expectTrue(
        name,
        result.status.ok() && result.outcome == Logger::Types::Init::Outcome::Started,
        std::format(
            "status={} native={} outcome={} requested={} effective={} outputStatus={} outputNative={}",
            static_cast<int>(result.status.code),
            result.status.nativeCode,
            static_cast<int>(result.outcome),
            static_cast<int>(result.requestedOutput),
            static_cast<int>(result.effectiveOutput),
            static_cast<int>(result.outputSetupStatus.code),
            result.outputSetupStatus.nativeCode));
}

void testInitResultModel(TestContext &context)
{
    ScopedLoggerShutdown shutdown;

    Logger::Types::Config disabled = makeConsoleConfig();
    disabled.output = Logger::Types::OutputMode::None;
    const Logger::Types::Init::Result disabledResult = Logger::init(disabled);
    context.expectTrue("disabled init status success", disabledResult.status.ok());
    context.expectEq("disabled init outcome", disabledResult.outcome, Logger::Types::Init::Outcome::Disabled);
    context.expectEq("disabled init effective output", disabledResult.effectiveOutput, Logger::Types::OutputMode::None);
    context.expectFalse("disabled init not running", Logger::running());
    static_cast<void>(Logger::shutdown());

    Logger::Types::Config adjusted = makeConsoleConfig();
    adjusted.maxQueueSize = 0;
    adjusted.maxMessageLength = 0;
    adjusted.inlineMessageCapacity = 4096;
    adjusted.workerBatchSize = 9999;
    const Logger::Types::Init::Result adjustedResult = Logger::init(adjusted);
    expectStarted(context, "adjusted init succeeds", adjustedResult);
    context.expectTrue(
        "queue adjustment reported",
        Logger::Types::Init::hasAdjustment(adjustedResult.adjustments, Logger::Types::Init::Adjustment::QueueLimitsAdjusted));
    context.expectTrue(
        "message adjustment reported",
        Logger::Types::Init::hasAdjustment(adjustedResult.adjustments, Logger::Types::Init::Adjustment::MessageLengthAdjusted));
    context.expectTrue(
        "inline adjustment reported",
        Logger::Types::Init::hasAdjustment(adjustedResult.adjustments, Logger::Types::Init::Adjustment::InlineCapacityAdjusted));
    context.expectTrue(
        "worker batch adjustment reported",
        Logger::Types::Init::hasAdjustment(adjustedResult.adjustments, Logger::Types::Init::Adjustment::WorkerBatchAdjusted));

    const Logger::Types::Init::Result alreadyRunning = Logger::init(makeConsoleConfig());
    context.expectEq("second init is AlreadyOpen", alreadyRunning.status.code, IO::Types::ErrorCode::AlreadyOpen);
    context.expectEq("second init preserves started state", alreadyRunning.outcome, Logger::Types::Init::Outcome::Started);
    context.expectTrue("second init leaves existing Logger running", Logger::running());
    static_cast<void>(Logger::shutdown());

    Logger::Types::Config storageFallback = makeConsoleConfig();
    storageFallback.maxQueueSize = std::numeric_limits<std::size_t>::max();
    storageFallback.workerBatchSize = 9999;
    const Logger::Types::Init::Result storageFallbackResult = Logger::init(storageFallback);
    expectStarted(context, "queue storage fallback succeeds", storageFallbackResult);
    context.expectTrue(
        "queue storage fallback reported",
        Logger::Types::Init::hasAdjustment(storageFallbackResult.adjustments, Logger::Types::Init::Adjustment::QueueStorageFallback));
    context.expectTrue(
        "fallback worker batch adjustment reported",
        Logger::Types::Init::hasAdjustment(storageFallbackResult.adjustments, Logger::Types::Init::Adjustment::WorkerBatchAdjusted));
    static_cast<void>(Logger::shutdown());

    Logger::Types::Config invalidOutput = makeConsoleConfig();
    invalidOutput.output = static_cast<Logger::Types::OutputMode>(99);
    context.expectEq("invalid output rejected", Logger::init(invalidOutput).status.code, IO::Types::ErrorCode::InvalidArgument);

    Logger::Types::Config invalidLevel = makeConsoleConfig();
    invalidLevel.minLevel = static_cast<Logger::Types::Level>(99);
    context.expectEq("invalid level rejected", Logger::init(invalidLevel).status.code, IO::Types::ErrorCode::InvalidArgument);

    Logger::Types::Config invalidPolicy = makeConsoleConfig();
    invalidPolicy.formatPolicy = static_cast<Logger::Types::FormatPolicy>(99);
    context.expectEq("invalid format policy rejected", Logger::init(invalidPolicy).status.code, IO::Types::ErrorCode::InvalidArgument);

    const std::string invalidUtf8{"\xC3\x28", 2};
    Logger::Types::Config invalidDirectory = makeConsoleConfig();
    invalidDirectory.logDirectory = invalidUtf8;
    context.expectEq("invalid UTF-8 directory rejected", Logger::init(invalidDirectory).status.code, IO::Types::ErrorCode::EncodingFailed);

    std::array invalidUtf8Sources{Logger::Types::SourceDefinition{1, std::string_view{"\xC3\x28", 2}}};
    Logger::Types::Config invalidSourceText = makeConsoleConfig();
    invalidSourceText.sources = invalidUtf8Sources;
    context.expectEq("invalid UTF-8 source rejected", Logger::init(invalidSourceText).status.code, IO::Types::ErrorCode::EncodingFailed);
}

void testFileFallbackAndSetupStatus(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    const std::filesystem::path blockingPath = context.logRoot / "not-a-directory";
    {
        std::ofstream file(blockingPath);
        file << "blocks directory creation";
    }
    const std::string blockingText = pathText(blockingPath);

    Logger::Types::Config noFallback = makeConsoleConfig();
    noFallback.output = Logger::Types::OutputMode::File;
    noFallback.logDirectory = blockingText;
    noFallback.fallbackToConsoleOnFileFailure = false;
    const Logger::Types::Init::Result failed = Logger::init(noFallback);
    context.expectFalse("file setup without fallback fails overall", failed.status.ok());
    context.expectFalse("file setup failure retained", failed.outputSetupStatus.ok());
    context.expectEq("file setup failure disabled", failed.outcome, Logger::Types::Init::Outcome::Disabled);
    context.expectEq("file setup failure output none", failed.effectiveOutput, Logger::Types::OutputMode::None);
    static_cast<void>(Logger::shutdown());

    Logger::Types::Config fallback = noFallback;
    fallback.fallbackToConsoleOnFileFailure = true;
    const Logger::Types::Init::Result recovered = Logger::init(fallback);
    expectStarted(context, "file setup fallback starts", recovered);
    context.expectFalse("fallback retains requested file failure", recovered.outputSetupStatus.ok());
    context.expectEq("fallback effective output console", recovered.effectiveOutput, Logger::Types::OutputMode::Console);
    context.expectEq("fallback health degraded", Logger::getHealth().state, Logger::Types::Health::State::Degraded);
}
