/// @file configuration_test.inl
/// @brief Logger initialization, configuration, and file-output tests.

void expectStarted(TestContext &context, std::string_view name, const Logger::Types::Init::Result &result)
{
    const Logger::Types::Init::Info *info = result.hasValue() ? &result.value() : nullptr;
    context.expectTrue(
        name,
        result.ok() && info != nullptr && info->outcome == Logger::Types::Init::Outcome::Started,
        std::format(
            "status={} outcome={} requested={} effective={} outputStatus={}",
            static_cast<int>(result.status().code()),
            info == nullptr ? -1 : static_cast<int>(info->outcome),
            info == nullptr ? -1 : static_cast<int>(info->requestedOutput),
            info == nullptr ? -1 : static_cast<int>(info->effectiveOutput),
            info == nullptr || !info->outputSetupStatus ? -1 : static_cast<int>(info->outputSetupStatus->code())));
}

void testInitResultModel(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;

    // Verify disabled output and its reported state.
    Logger::Types::Config disabled = makeConsoleConfig();
    disabled.output = Logger::Types::OutputMode::None;
    const Logger::Types::Init::Result disabledResult = Logger::init(disabled);
    context.expectTrue("disabled init status success", disabledResult.ok());
    if(disabledResult.hasValue())
    {
        context.expectEq("disabled init outcome", disabledResult.value().outcome, Logger::Types::Init::Outcome::Disabled);
        context.expectEq("disabled init effective output", disabledResult.value().effectiveOutput, Logger::Types::OutputMode::None);
    }
    context.expectFalse("disabled init not running", Logger::running());
    static_cast<void>(Logger::shutdown());

    // Verify out-of-range settings are adjusted and reported.
    Logger::Types::Config adjusted = makeConsoleConfig();
    adjusted.maxQueueSize = 0;
    adjusted.maxMessageLength = 0;
    adjusted.inlineMessageCapacity = 4096;
    adjusted.workerBatchSize = 9999;
    const Logger::Types::Init::Result adjustedResult = Logger::init(adjusted);
    expectStarted(context, "adjusted init succeeds", adjustedResult);
    if(adjustedResult.hasValue())
    {
        context.expectTrue(
            "queue adjustment reported",
            Logger::Types::Init::hasAdjustment(adjustedResult.value().adjustments, Logger::Types::Init::Adjustment::QueueLimitsAdjusted));
        context.expectTrue(
            "message adjustment reported",
            Logger::Types::Init::hasAdjustment(adjustedResult.value().adjustments, Logger::Types::Init::Adjustment::MessageLengthAdjusted));
        context.expectTrue(
            "inline adjustment reported",
            Logger::Types::Init::hasAdjustment(adjustedResult.value().adjustments, Logger::Types::Init::Adjustment::InlineCapacityAdjusted));
        context.expectTrue(
            "worker batch adjustment reported",
            Logger::Types::Init::hasAdjustment(adjustedResult.value().adjustments, Logger::Types::Init::Adjustment::WorkerBatchAdjusted));
    }

    const Logger::Types::Init::Result alreadyRunning = Logger::init(makeConsoleConfig());
    context.expectEq("second init is AlreadyOpen", alreadyRunning.status().code(), GameWIP::Status::Types::Code::AlreadyOpen);
    context.expectTrue("second init preserves prior init result", alreadyRunning.hasValue());
    if(alreadyRunning.hasValue())
    {
        context.expectEq("second init preserves started state", alreadyRunning.value().outcome, Logger::Types::Init::Outcome::Started);
    }
    context.expectTrue("second init leaves existing Logger running", Logger::running());
    static_cast<void>(Logger::shutdown());

    // Verify storage fallback remains usable when the requested queue is too large.
    Logger::Types::Config storageFallback = makeConsoleConfig();
    storageFallback.maxQueueSize = std::numeric_limits<std::size_t>::max();
    storageFallback.workerBatchSize = 9999;
    const Logger::Types::Init::Result storageFallbackResult = Logger::init(storageFallback);
    expectStarted(context, "queue storage fallback succeeds", storageFallbackResult);
    if(storageFallbackResult.hasValue())
    {
        context.expectTrue(
            "queue storage fallback reported",
            Logger::Types::Init::hasAdjustment(storageFallbackResult.value().adjustments, Logger::Types::Init::Adjustment::QueueStorageFallback));
        context.expectTrue(
            "fallback worker batch adjustment reported",
            Logger::Types::Init::hasAdjustment(storageFallbackResult.value().adjustments, Logger::Types::Init::Adjustment::WorkerBatchAdjusted));
    }
    static_cast<void>(Logger::shutdown());

    // Reject unsupported enum values and malformed layout settings.
    Logger::Types::Config invalidOutput = makeConsoleConfig();
    invalidOutput.output = static_cast<Logger::Types::OutputMode>(99);
    context.expectEq("invalid output rejected", Logger::init(invalidOutput).status().code(), GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidLevel = makeConsoleConfig();
    invalidLevel.minLevel = static_cast<Logger::Types::Level>(99);
    context.expectEq("invalid level rejected", Logger::init(invalidLevel).status().code(), GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidPolicy = makeConsoleConfig();
    invalidPolicy.formatPolicy = static_cast<Logger::Types::FormatPolicy>(99);
    context.expectEq("invalid format policy rejected", Logger::init(invalidPolicy).status().code(), GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidTheme = makeConsoleConfig();
    invalidTheme.theme.error = {.kind = Logger::Color::Kind::Basic, .basic = static_cast<Logger::Color::Basic>(99)};
    context.expectEq("invalid theme color rejected", Logger::init(invalidTheme).status().code(), GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidFieldLayout = makeConsoleConfig();
    invalidFieldLayout.fieldLayout = static_cast<Logger::FieldLayout>(99);
    context.expectEq(
        "invalid field layout rejected",
        Logger::init(invalidFieldLayout).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidLayout = makeConsoleConfig();
    invalidLayout.layout = {"unknown {placeholder}"};
    context.expectEq("invalid runtime layout rejected", Logger::init(invalidLayout).status().code(), GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidTimestampFormat = makeConsoleConfig();
    invalidTimestampFormat.timestampFormat = "%H:%M:%S%";
    context.expectEq(
        "timestamp format with a dangling directive rejected",
        Logger::init(invalidTimestampFormat).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config emptyTimestampFormat = makeConsoleConfig();
    emptyTimestampFormat.timestampFormat = {};
    context.expectEq(
        "empty timestamp format rejected",
        Logger::init(emptyTimestampFormat).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidAlignment = makeConsoleConfig();
    invalidAlignment.layout = {"{area:x12}"};
    context.expectEq(
        "unsupported alignment specifier rejected",
        Logger::init(invalidAlignment).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidAutomaticWidth = makeConsoleConfig();
    invalidAutomaticWidth.layout = {"{message:<auto}"};
    context.expectEq(
        "automatic width on a free-form value rejected",
        Logger::init(invalidAutomaticWidth).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config invalidAlignmentWidth = makeConsoleConfig();
    invalidAlignmentWidth.layout = {"{level:>x}"};
    context.expectEq(
        "invalid placeholder alignment width rejected",
        Logger::init(invalidAlignmentWidth).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    Logger::Types::Config unclosedColorScope = makeConsoleConfig();
    unclosedColorScope.layout = {"{@area}[{area}]"};
    context.expectEq(
        "unclosed runtime layout color scope rejected",
        Logger::init(unclosedColorScope).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);

    const std::string invalidUtf8{"\xC3\x28", 2};
    Logger::Types::Config invalidDirectory = makeConsoleConfig();
    invalidDirectory.logDirectory = invalidUtf8;
    context.expectEq(
        "invalid UTF-8 directory rejected",
        Logger::init(invalidDirectory).status().code(),
        GameWIP::Status::Types::Code::EncodingFailed);

    Logger::Types::Config invalidLayoutEncoding = makeConsoleConfig();
    invalidLayoutEncoding.layout = {invalidUtf8};
    context.expectEq(
        "invalid UTF-8 layout rejected",
        Logger::init(invalidLayoutEncoding).status().code(),
        GameWIP::Status::Types::Code::EncodingFailed);

    Logger::Types::Config invalidTimestampEncoding = makeConsoleConfig();
    invalidTimestampEncoding.timestampFormat = invalidUtf8;
    context.expectEq(
        "invalid UTF-8 timestamp format rejected",
        Logger::init(invalidTimestampEncoding).status().code(),
        GameWIP::Status::Types::Code::EncodingFailed);

    std::array invalidUtf8Sources{Logger::Types::SourceDefinition{1, std::string_view{"\xC3\x28", 2}}};
    Logger::Types::Config invalidSourceText = makeConsoleConfig();
    invalidSourceText.sources = invalidUtf8Sources;
    context.expectEq("invalid UTF-8 source rejected", Logger::init(invalidSourceText).status().code(), GameWIP::Status::Types::Code::EncodingFailed);

    const Logger::Color invalidColorValue{.kind = static_cast<Logger::Color::Kind>(99)};
    std::array invalidColorSources{Logger::Types::SourceDefinition{1, "InvalidColor", invalidColorValue}};
    Logger::Types::Config invalidColorConfig = makeConsoleConfig();
    invalidColorConfig.sources = invalidColorSources;
    context.expectEq(
        "invalid source color rejected",
        Logger::init(invalidColorConfig).status().code(),
        GameWIP::Status::Types::Code::InvalidArgument);
}

void testFileFallbackAndSetupStatus(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;
    const std::filesystem::path blockingPath = context.logRoot / "not-a-directory";

    // Create a file where Logger needs a directory.
    bool blockingFileReady = false;
    {
        std::ofstream file(blockingPath);
        file << "blocks directory creation";
        file.close();
        blockingFileReady = file.good();
    }

    context.expectTrue("blocking file fixture writes", blockingFileReady);

    if(!blockingFileReady)
    {
        return;
    }

    const std::string blockingText = pathText(context, blockingPath);

    if(blockingText.empty())
    {
        return;
    }

    Logger::Types::Config noFallback = makeConsoleConfig();
    noFallback.output = Logger::Types::OutputMode::File;
    noFallback.logDirectory = blockingText;
    noFallback.fallbackToConsoleOnFileFailure = false;
    const Logger::Types::Init::Result failed = Logger::init(noFallback);
    context.expectFalse("file setup without fallback fails overall", failed.ok());
    context.expectTrue("file setup failure retained", failed.hasValue() && failed.value().outputSetupStatus.has_value());
    if(failed.hasValue())
    {
        context.expectEq("file setup failure disabled", failed.value().outcome, Logger::Types::Init::Outcome::Disabled);
        context.expectEq("file setup failure output none", failed.value().effectiveOutput, Logger::Types::OutputMode::None);
    }
    static_cast<void>(Logger::shutdown());

    // Confirm the configured fallback starts Logger and preserves the setup error.
    Logger::Types::Config fallback = noFallback;
    fallback.fallbackToConsoleOnFileFailure = true;
    const Logger::Types::Init::Result recovered = Logger::init(fallback);
    expectStarted(context, "file setup fallback starts", recovered);
    if(recovered.hasValue())
    {
        context.expectTrue("fallback retains requested file failure", recovered.value().outputSetupStatus.has_value());
        context.expectEq("fallback effective output console", recovered.value().effectiveOutput, Logger::Types::OutputMode::Console);
    }
    context.expectEq("fallback health degraded", Logger::getHealth().state, Logger::Types::Health::State::Degraded);
}
