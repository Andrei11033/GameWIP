/// @file output_test.inl
/// @brief Logger output and UTF-8 truncation tests.

void testFileLoggingAndUtf8Truncation(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "utf8-truncation");
    config.maxMessageLength = 24;
    config.inlineMessageCapacity = 16;
    expectStarted(context, "UTF-8 file init", Logger::init(config.ready()));

    const std::string message = "prefix \xF0\x9F\x98\x80 \xE2\x98\x85 payload that truncates";
    Logger::info(kTestSource, message);
    Logger::info(kTestSource, "formatted {}", message);
    const Logger::Types::Report::Result report = Logger::reportError(kTestSource, "formatted {}", message);
    context.expectTrue("UTF-8 formatted report delivered", reportDelivered(report));
    context.expectTrue("UTF-8 file flush", flushCompleted(Logger::flush(2s)));
    const std::filesystem::path logFile = loggerLogFilePath(context);
    static_cast<void>(Logger::shutdown());

    const std::string contents = readWholeFile(context, logFile);

    context.expectContains("truncation suffix written", contents, "[truncated]");
    context.expectTrue("UTF-8 prefix retained", contents.contains("prefix "));
    context.expectContains("strict formatted truncation suffix written", contents, "formatted... [truncated]");
    context.expectEq(
        "Logger-owned truncation preserves valid UTF-8",
        GameWIP::Unicode::Utf8::validate(contents).outcome,
        GameWIP::Unicode::Types::ValidationOutcome::Valid);
}

void testConfigurableTimestampFormatting(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "timestamp-format");
    config.timestampFormat = "literal=%%f ms=%f";
    expectStarted(context, "custom timestamp format init", Logger::init(config.ready()));

    Logger::info(kTestSource, "timestamp format check");
    context.expectTrue("custom timestamp format flush", flushCompleted(Logger::flush(2s)));
    const std::string contents = readWholeFile(context, loggerLogFilePath(context));
    static_cast<void>(Logger::shutdown());

    constexpr std::string_view prefix = "[literal=%f ms=";
    const std::size_t timestampStart = contents.find(prefix);
    if(timestampStart == std::string::npos)
    {
        context.expectTrue("custom timestamp format prefix rendered", false, contents);
        return;
    }
    const std::size_t millisecondsStart = timestampStart + prefix.size();
    const bool hasThreeMillisecondDigits = millisecondsStart + 3 < contents.size() && contents[millisecondsStart] >= '0' &&
                                           contents[millisecondsStart] <= '9' && contents[millisecondsStart + 1] >= '0' &&
                                           contents[millisecondsStart + 1] <= '9' && contents[millisecondsStart + 2] >= '0' &&
                                           contents[millisecondsStart + 2] <= '9' && contents[millisecondsStart + 3] == ']';
    context.expectTrue("custom timestamp `%f` expands to three digits", hasThreeMillisecondDigits, contents);
}

void testMarkedValuesAndRecordFields(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
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

    const std::filesystem::path logFile = loggerLogFilePath(context);
    static_cast<void>(Logger::shutdown());
    const std::string contents = readWholeFile(context, logFile);

    context.expectContains("rich record fields retained", contents, "device=Keyboard id=12");
    context.expectContains("rich report fields retained", contents, "report=true: Emergency report");
    context.expectContains("rich record source metadata retained", contents, "thread=");
    context.expectContains("marked values retain plain text", contents, "Loaded code 404 from settings.ini with 128 bytes and ready");
    context.expectContains("valid control characters survive plain output", contents, controlText);
    context.expectContains("valid controls survive marked values", contents, "colored " + controlText);
    context.expectContains("Record compile-time format API", contents, "runtime 42");
    context.expectContains("Record runtime format API", contents, "runtime 43");
    context.expectContains("Record runtime report API", contents, "runtime report 44");
    context.expectContains("compact Record layout", contents, "layout=compact: Compact layout");
    context.expectContains("expanded Record layout", contents, "Expanded layout\n    layout=expanded");
    context.expectContains("parenthesized Record layout", contents, "(layout=parenthesized) Parenthesized layout");
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
    ScopedLoggerShutdown shutdown;
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
            customContents.contains("Device report device=Mouse") && customContents.contains("output_test.inl") &&
            customContents.contains("thread=") && customContents.contains("process="),
        customContents);
    context.expectContains("custom layout preserves message spacing", customContents, "left  middle   right");
    context.expectContains("Record owns custom layout text", customContents, "Record owns its layout text");
    static_cast<void>(Logger::shutdown());

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
    ScopedLoggerShutdown shutdown;
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
    ScopedLoggerShutdown shutdown;
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
    ScopedLoggerShutdown shutdown;
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
    ScopedLoggerShutdown shutdown;
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

void testConsoleThemeAndAreaColors(TestContext &context)
{
#if LOGGER_INTERNAL_TEST_HOOKS && TERMINAL_INTERNAL_TEST_HOOKS
    ScopedLoggerShutdown shutdown;
    using GameWIP::Terminal::Types::Output::Capabilities;
    using GameWIP::Terminal::Types::Output::Stream;
    const Stream stream = Stream::Stdout;
    const std::array sources{Logger::defineSource(TestSource::Core, "Area]WithBracket", Logger::Color::rgb(0, 255, 0))};

    constexpr auto customColorLayout =
        Logger::makeLayout("{@timestamp}[{time}]{/}{@area}[{area}]{/}{@severity}[{level}]{/} {@area}<{@code}nested{/}>{/} {@message}{message}{/}");
    const auto captureMessage = [&](bool rgbSupported, bool messageUsesSeverityColor, bool useCustomLayout)
    {
        static_cast<void>(Logger::shutdown());
        GameWIP::Terminal::TestHooks::reset();
        Capabilities capabilities{
            .kind = GameWIP::Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteOutput = true,
            .supportsFlush = true,
            .style = {.basicColor = true, .rgbColor = rgbSupported}};
        GameWIP::Terminal::TestHooks::setOutputCapabilitiesOverride(stream, capabilities);
        GameWIP::Terminal::TestHooks::setPreparedOutputCapabilitiesOverride(stream, capabilities);
        GameWIP::Terminal::TestHooks::setOutputCapture(stream, true);

        Logger::Types::Config config = makeConsoleConfig();
        config.enableConsoleColor = true;
        config.enableDebugOutput = false;
        config.enableFatalPopup = false;
        config.sources = sources;
        config.theme.message = Logger::Color::rgb(255, 70, 70);
        config.theme.info = Logger::Color::rgb(0, 255, 0);
        config.theme.code = Logger::Color::rgb(200, 40, 180);
        config.theme.messageUsesSeverityColor = messageUsesSeverityColor;
        if(useCustomLayout)
        {
            config.layout = customColorLayout;
        }
        expectStarted(context, "themed console init", Logger::init(config));

        Logger::trace(TestSource::Core, "trace probe");
        Logger::info(TestSource::Core, "color probe");
        Logger::info(TestSource::Core, "base {} tail", Logger::code("marked"));
        context.expectTrue("themed console flush", flushCompleted(Logger::flush(2s)));

        const std::string output = GameWIP::Terminal::TestHooks::capturedOutputText(stream);
        static_cast<void>(Logger::shutdown());
        return output;
    };

    const std::string basicSeverityOutput = captureMessage(false, true, false);
    context.expectContains("trace label and brackets use bright green", basicSeverityOutput, "\x1b[92m[TRACE]\x1b[0m");
    context.expectContains("message inherits RGB severity color on basic terminal", basicSeverityOutput, "\x1b[92m color probe\x1b[0m");
    context.expectContains("timestamp brackets use the timestamp color", basicSeverityOutput, "\x1b[90m[");
    context.expectContains("area label and brackets use the area color", basicSeverityOutput, "\x1b[92m[Area]WithBracket]\x1b[0m");
    context.expectContains("severity label and brackets use the severity color", basicSeverityOutput, "\x1b[92m[INFO ]\x1b[0m");
    context.expectContains("marked role keeps its basic terminal color", basicSeverityOutput, "\x1b[35mmarked\x1b[0m");

    const std::string basicThemeOutput = captureMessage(false, false, false);
    context.expectContains("message uses configured color when severity coloring is disabled", basicThemeOutput, "\x1b[91m color probe\x1b[0m");

    const std::string rgbSeverityOutput = captureMessage(true, true, false);
    context.expectContains("message inherits RGB severity color on RGB terminal", rgbSeverityOutput, "\x1b[38;2;0;255;0m color probe\x1b[0m");
    context.expectContains("marked role keeps its RGB terminal color", rgbSeverityOutput, "\x1b[38;2;200;40;180mmarked\x1b[0m");

    const std::string rgbThemeOutput = captureMessage(true, false, false);
    context.expectContains(
        "message uses configured RGB color when severity coloring is disabled",
        rgbThemeOutput,
        "\x1b[38;2;255;70;70m color probe\x1b[0m");
    context.expectContains("RGB area label and brackets use full terminal RGB", rgbThemeOutput, "\x1b[38;2;0;255;0m[Area]WithBracket]\x1b[0m");

    const std::string customLayoutOutput = captureMessage(true, true, true);
    context.expectContains("custom layout area scope colors its bracketed label", customLayoutOutput, "\x1b[38;2;0;255;0m[Area]WithBracket]\x1b[0m");
    context.expectContains("custom layout severity scope colors its bracketed label", customLayoutOutput, "\x1b[38;2;0;255;0m[INFO]\x1b[0m");
    context.expectContains("nested layout color scopes apply their inner role", customLayoutOutput, "\x1b[38;2;200;40;180mnested\x1b[0m");
    context.expectContains("message color scope preserves explicitly marked value colors", customLayoutOutput, "\x1b[38;2;200;40;180mmarked\x1b[0m");
    GameWIP::Terminal::TestHooks::reset();
#else
    context.skip("console theme and area color test", "Logger or Terminal internal test hooks are disabled");
#endif
}
