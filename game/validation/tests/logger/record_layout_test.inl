/// @file record_layout_test.inl
/// @brief Logger structured Record formatting tests.

/// ------------------------------------------------------------
/// Records and layout formatting
/// ------------------------------------------------------------

void testMarkedValuesAndRecordFields(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;

    // Verify the default theme and named colors.
    const Logger::Theme defaultTheme{};
    context.expectTrue(
        "named cyan constant is basic cyan",
        Logger::Color::Cyan.kind == Logger::Color::Kind::Basic && Logger::Color::Cyan.basic == Logger::Color::Basic::Cyan);
    context.expectTrue(
        "named bright green constant is basic bright green",
        Logger::Color::BrightGreen.kind == Logger::Color::Kind::Basic && Logger::Color::BrightGreen.basic == Logger::Color::Basic::BrightGreen);
    context.expectTrue(
        "default message color is white",
        defaultTheme.message.kind == Logger::Color::Kind::Basic && defaultTheme.message.basic == Logger::Color::Basic::White);
    context.expectTrue("messages use severity color by default", defaultTheme.messageUsesSeverityColor);
    context.expectTrue(
        "default trace color is bright green",
        defaultTheme.trace.kind == Logger::Color::Kind::Basic && defaultTheme.trace.basic == Logger::Color::Basic::BrightGreen);
    context.expectTrue(
        "default timestamp color is muted gray",
        defaultTheme.timestamp.kind == Logger::Color::Kind::Basic && defaultTheme.timestamp.basic == Logger::Color::Basic::BrightBlack);

    // Emit marked values, metadata, reports, and built-in layouts.
    OwnedLoggerConfig config = makeFileConfig(context, "rich-records");
    const std::array sources{
        Logger::defineSource(TestSource::Core, "Input", Logger::Color::rgb(120, 150, 255)),
        Logger::defineSource(TestSource::Render, "Rendering")};
    config.sources = sources;
    expectStarted(context, "rich-record init", Logger::init(config.ready()));

    Logger::info(
        TestSource::Core,
        Logger::record().field("device", "Keyboard").field("id", 12).threadId().processId().sourceLocation(),
        "Device connected");
    Logger::info(
        TestSource::Core,
        "Loaded code {} from {} with {} bytes and {}",
        Logger::code(404),
        Logger::path("settings.ini"),
        Logger::number(128),
        Logger::color("ready", Logger::Color::rgb(200, 100, 240)));

    std::string controlText = "literal";
    controlText.push_back('\x1E');
    controlText.append(" middle ");
    controlText.push_back('\x1F');
    controlText.append(" end ");
    controlText.push_back('\x1D');
    controlText.push_back('\x1C');
    Logger::info(TestSource::Core, controlText);
    Logger::info(TestSource::Core, "colored {}", Logger::code(controlText));

    Logger::info(TestSource::Core, Logger::record(), "runtime {}", 42);
    Logger::info(TestSource::Core, Logger::record(), Logger::runtimeFormat("runtime {}"), 43);
    const Logger::Types::Report::Result richReport =
        Logger::reportError(TestSource::Core, Logger::record().field("report", true), "Emergency report");
    const Logger::Types::Report::Result runtimeReport =
        Logger::report(Logger::Types::Level::Error, TestSource::Core, Logger::record(), Logger::runtimeFormat("runtime report {}"), 44);
    context.expectTrue("Record report delivered", reportDelivered(richReport));
    context.expectTrue("runtime Record report delivered", reportDelivered(runtimeReport));

    Logger::info(TestSource::Core, Logger::record().field("layout", "compact").layout(Logger::Layouts::compact()), "Compact layout");
    Logger::info(TestSource::Core, Logger::record().field("layout", "expanded").layout(Logger::Layouts::expanded()), "Expanded layout");
    Logger::info(
        TestSource::Core,
        Logger::record().field("layout", "parenthesized").layout(Logger::Layouts::parenthesized()),
        "Parenthesized layout");
    context.expectTrue("rich records file flush", flushCompleted(Logger::flush(2s)));

    // Check the rendered file output.
    const std::filesystem::path logFile = loggerLogFilePath(context);
    static_cast<void>(Logger::shutdown());
    const std::string contents = readWholeFile(context, logFile);

    context.expectContains("adaptive Record fields follow the message", contents, "Device connected (device=Keyboard id=12");
    context.expectContains("rich report fields retained", contents, "Emergency report (report=true)");
    context.expectContains("rich record source metadata retained", contents, "thread=");
    context.expectContains("marked values retain plain text", contents, "Loaded code 404 from settings.ini with 128 bytes and ready");
    context.expectContains("valid control characters survive plain output", contents, controlText);
    context.expectContains("valid controls survive marked values", contents, "colored " + controlText);
    context.expectContains("Record compile-time format API", contents, "runtime 42");
    context.expectContains("Record runtime format API", contents, "runtime 43");
    context.expectContains("Record runtime report API", contents, "runtime report 44");
    context.expectContains("compact Record layout", contents, "layout=compact: Compact layout");
    context.expectContains("expanded Record layout", contents, "Expanded layout\n    layout=expanded");
    context.expectContains("parenthesized Record layout", contents, "Parenthesized layout (layout=parenthesized)");
    context.expectFalse(
        "internal marker bytes stripped from file",
        contents.contains(static_cast<char>(0xFF)) || contents.contains(static_cast<char>(0xFE)) || contents.contains(static_cast<char>(0xFD)) ||
            contents.contains(static_cast<char>(0xFC)) || contents.contains(static_cast<char>(0xFB)) || contents.contains(static_cast<char>(0xFA)) ||
            contents.contains(static_cast<char>(0xF8)) || contents.contains(static_cast<char>(0xF7)) || contents.contains(static_cast<char>(0xF6)));
    context.expectContains("default area aligns to the longest registered source", contents, "][Input    ][INFO ]");
    const std::size_t timestampStart = contents.find('[');
    const std::size_t timestampEnd = contents.find(']', timestampStart);
    const std::size_t decimalPoint = contents.find('.', timestampStart);
    context.expectTrue(
        "default timestamp includes milliseconds",
        decimalPoint != std::string::npos && timestampEnd != std::string::npos && timestampEnd > decimalPoint && timestampEnd - decimalPoint == 4);
}

void testCustomRecordLayoutsAndOwnership(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;

    // Verify custom layout placeholders and copied Record layout text.
    constexpr auto customLayout = Logger::makeLayout(
        "{time} {@severity}[{level}]{/} {@area}[{area}]{/} {source_file}:{source_line} {source_function} thread={thread_id} process={process_id} "
        "{message} {fields}");
    OwnedLoggerConfig customConfig = makeFileConfig(context, "custom-layout");
    const std::array customSources{Logger::defineSource(TestSource::Core, "Input")};
    customConfig.sources = customSources;
    customConfig.layout = customLayout;
    expectStarted(context, "custom layout init", Logger::init(customConfig.ready()));

    Logger::info(
        TestSource::Core,
        Logger::record().field("device", "Keyboard").sourceLocation().threadId().processId().layout(customLayout),
        "Device connected");
    const Logger::Types::Report::Result customLayoutReport =
        Logger::reportError(TestSource::Core, Logger::record().field("device", "Mouse").layout(customLayout), "Device report");
    context.expectTrue("custom Record layout report delivered", reportDelivered(customLayoutReport));

    constexpr auto messageOnlyLayout = Logger::makeLayout("{message}");
    Logger::info(TestSource::Core, Logger::record().layout(messageOnlyLayout), "left  middle   right");
    std::string temporaryRecordLayoutText = "{message}";
    const Logger::Record copiedRecordLayout = Logger::record().layout(Logger::LayoutTemplate{temporaryRecordLayoutText});
    temporaryRecordLayoutText.clear();
    Logger::info(TestSource::Core, copiedRecordLayout, "Record owns its layout text");
    context.expectTrue("custom layout flush", flushCompleted(Logger::flush(2s)));

    const std::string customContents = readWholeFile(context, loggerLogFilePath(context));

    context.expectTrue(
        "custom layout placeholders rendered",
        customContents.contains("[INFO] [Input]") && customContents.contains("Device connected device=Keyboard") &&
            customContents.contains("Device report device=Mouse") && customContents.contains("record_layout_test.inl") &&
            customContents.contains("thread=") && customContents.contains("process="),
        customContents);
    context.expectContains("custom layout preserves message spacing", customContents, "left  middle   right");
    context.expectContains("Record owns custom layout text", customContents, "Record owns its layout text");
    static_cast<void>(Logger::shutdown());

    // Verify Config retains layout text after its source string expires.
    OwnedLoggerConfig ownedLayoutConfig = makeFileConfig(context, "owned-layout");
    {
        const std::string temporaryLayout = "{message}";
        ownedLayoutConfig.layout = {temporaryLayout};
        expectStarted(context, "owned layout init", Logger::init(ownedLayoutConfig.ready()));
    }
    Logger::info(TestSource::Core, "layout survives Config and source text");
    context.expectTrue("owned layout flush", flushCompleted(Logger::flush(2s)));
    const std::string ownedLayoutContents = readWholeFile(context, loggerLogFilePath(context));
    static_cast<void>(Logger::shutdown());
    context.expectContains("Config owns layout text after init", ownedLayoutContents, "layout survives Config and source text");
}

void testRecordFieldLimits(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig boundedFieldsConfig = makeFileConfig(context, "bounded-record-fields");
    boundedFieldsConfig.fieldLayout = Logger::Layouts::expanded();
    boundedFieldsConfig.maxRecordFields = 1;
    boundedFieldsConfig.maxFieldKeyLength = 4;
    boundedFieldsConfig.maxFieldValueLength = 5;
    expectStarted(context, "bounded Record fields init", Logger::init(boundedFieldsConfig.ready()));

    Logger::info(kTestSource, Logger::record().field("longKey", "abcdef").field("ignored", 2), "Global layout message");
    context.expectTrue("bounded Record fields flush", flushCompleted(Logger::flush(2s)));
    const std::string boundedFieldsContents = readWholeFile(context, loggerLogFilePath(context));
    static_cast<void>(Logger::shutdown());

    context.expectContains("Config field layout applies globally", boundedFieldsContents, "Global layout message\n    long=abcde");
    context.expectFalse("Record field count limit drops later fields", boundedFieldsContents.contains("ignored"));
}

void testRecordFormattingHonorsMessageBound(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "record-message-bound");
    config.maxMessageLength = 32;
    config.maxFieldValueLength = 256;
    expectStarted(context, "bounded Record init", Logger::init(config.ready()));

    const std::string largeValue(200, 'V');
    Logger::info(kTestSource, Logger::record().field("detail", largeValue), "message {}", largeValue);
    Logger::info(kTestSource, Logger::record().field("detail", largeValue), largeValue);
    const Logger::Types::Report::Result boundedReport = Logger::reportError(kTestSource, Logger::record(), largeValue);
    context.expectTrue("bounded Record report delivered", reportDelivered(boundedReport));
    context.expectTrue("bounded Record flush", flushCompleted(Logger::flush(2s)));
    const std::string contents = readWholeFile(context, loggerLogFilePath(context));
    static_cast<void>(Logger::shutdown());

    // Locate the message and verify its byte bound and UTF-8 validity.
    const std::size_t lineStart = contents.find('[');
    const std::size_t lineEnd = contents.find('\n', lineStart);
    if(lineStart == std::string::npos || lineEnd == std::string::npos || lineEnd < lineStart)
    {
        context.expectTrue("bounded Record line is present", false, contents);
        return;
    }
    const std::string_view line = std::string_view(contents).substr(lineStart, lineEnd - lineStart);
    const std::size_t areaEnd = line.find("] ");
    if(areaEnd == std::string_view::npos)
    {
        context.expectTrue("bounded Record line has a message section", false, line);
        return;
    }
    const std::size_t messageStart = areaEnd + 2;
    context.expectTrue(
        "Record output stays within the configured message byte bound",
        messageStart <= line.size() && line.size() - messageStart <= 32);
    context.expectContains("bounded Record output shows truncation", line, "[truncated]");
    context.expectEq(
        "bounded Record output remains valid UTF-8",
        GameWIP::Unicode::Utf8::validate(contents).outcome,
        GameWIP::Unicode::Types::ValidationOutcome::Valid);
    std::size_t truncationMarkerCount = 0;
    for(std::size_t position = 0; (position = contents.find("[truncated]", position)) != std::string::npos; position += 11)
    {
        ++truncationMarkerCount;
    }
    context.expectEq("each oversized Record message has one truncation marker", truncationMarkerCount, std::size_t{3});
}

void testLayoutAlignment(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;
    constexpr auto alignmentLayout =
        Logger::makeLayout("{area:<auto}|{area:^auto}|{area:>auto}|{level:<5}|{level:^5}|{level:>5}|{message:<16}|{message}");
    OwnedLoggerConfig config = makeFileConfig(context, "layout-alignment");
    const std::array sources{Logger::defineSource(TestSource::Core, "Input"), Logger::defineSource(TestSource::Render, "Rendering")};
    config.sources = sources;
    config.layout = alignmentLayout;
    expectStarted(context, "alignment layout init", Logger::init(config.ready()));

    Logger::info(TestSource::Core, "aligned labels");
    context.expectTrue("alignment layout flush", flushCompleted(Logger::flush(2s)));
    const std::string contents = readWholeFile(context, loggerLogFilePath(context));
    static_cast<void>(Logger::shutdown());

    context.expectContains("left, centered, and right auto-width area alignment", contents, "Input    |  Input  |    Input|INFO |INFO | INFO|");
    context.expectContains("explicit placeholder width aligns message text", contents, "aligned labels  |aligned labels");
}

void testTruncatedCompiledLayoutFallsBackSafely(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;
    constexpr auto longLayout = Logger::makeLayout("layout-prefix-that-exceeds-the-message-limit-{message}");
    OwnedLoggerConfig config = makeFileConfig(context, "truncated-compiled-layout");
    config.maxMessageLength = 32;
    expectStarted(context, "truncated compiled layout init", Logger::init(config.ready()));

    Logger::info(kTestSource, Logger::record().layout(longLayout), "bounded payload");
    context.expectTrue("truncated compiled layout flush", flushCompleted(Logger::flush(2s)));
    const std::string contents = readWholeFile(context, loggerLogFilePath(context));
    static_cast<void>(Logger::shutdown());

    context.expectContains("truncated layout still delivers a readable fallback", contents, "[truncated]");
    context.expectEq(
        "truncated compiled layout output remains valid UTF-8",
        GameWIP::Unicode::Utf8::validate(contents).outcome,
        GameWIP::Unicode::Types::ValidationOutcome::Valid);
    context.expectFalse("compiled layout marker never reaches output", contents.contains(static_cast<char>(0xF6)));
}
