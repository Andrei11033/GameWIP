/// @file output_test.inl
/// @brief Logger file output, timestamps, and console theme tests.

/// ------------------------------------------------------------
/// File output and timestamps
/// ------------------------------------------------------------

void testFileLoggingAndUtf8Truncation(TestContext &context)
{
    const ScopedLoggerShutdown shutdown;
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
    const ScopedLoggerShutdown shutdown;
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

/// ------------------------------------------------------------
/// Console themes
/// ------------------------------------------------------------

void testConsoleThemeAndAreaColors(TestContext &context)
{
#if LOGGER_INTERNAL_TEST_HOOKS && TERMINAL_INTERNAL_TEST_HOOKS
    const ScopedLoggerShutdown shutdown;
    using GameWIP::Terminal::Types::Output::Capabilities;
    using GameWIP::Terminal::Types::Output::Stream;
    const Stream stream = Stream::Stdout;
    const std::array sources{Logger::defineSource(TestSource::Core, "Area]WithBracket", Logger::Color::rgb(0, 255, 0))};

    constexpr auto customColorLayout =
        Logger::makeLayout("{@timestamp}[{time}]{/}{@area}[{area}]{/}{@severity}[{level}]{/} {@area}<{@code}nested{/}>{/} {@message}{message}{/}");
    const auto captureMessage = [&](bool rgbSupported, bool messageUsesSeverityColor, bool useCustomLayout)
    {
        static_cast<void>(Logger::shutdown());
        GameWIP::Terminal::Internal::TestHooks::reset();
        Capabilities capabilities{
            .kind = GameWIP::Terminal::Types::StreamKind::Terminal,
            .supportsUtf8Text = true,
            .supportsByteOutput = true,
            .supportsFlush = true,
            .style = {.basicColor = true, .rgbColor = rgbSupported}};
        GameWIP::Terminal::Internal::TestHooks::setOutputCapabilitiesOverride(stream, capabilities);
        GameWIP::Terminal::Internal::TestHooks::setPreparedOutputCapabilitiesOverride(stream, capabilities);
        GameWIP::Terminal::Internal::TestHooks::setOutputCapture(stream, true);

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

        const std::string output = GameWIP::Terminal::Internal::TestHooks::capturedOutputText(stream);
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
    GameWIP::Terminal::Internal::TestHooks::reset();
#else
    context.skip("console theme and area color test", "Logger or Terminal internal test hooks are disabled");
#endif
}
