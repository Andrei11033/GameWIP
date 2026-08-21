/// @file output_test.inl
/// @brief Logger normal output and UTF-8 truncation correctness suites.

void testFileLoggingAndUtf8Truncation(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "utf8-truncation");
    config.maxMessageLength = 24;
    config.inlineMessageCapacity = 16;
    expectStarted(context, "UTF-8 file init", Logger::init(config.ready()));

    const std::string message = "prefix \xF0\x9F\x98\x80 \xE2\x98\x85 payload that truncates";
    Logger::info(testSource, message);
    Logger::info(testSource, "formatted {}", message);
    const Logger::Types::Report::Result report = Logger::reportError(testSource, "formatted {}", message);
    context.expectTrue("UTF-8 formatted report delivered", reportDelivered(report));
    context.expectTrue("UTF-8 file flush", flushCompleted(Logger::flush(2s)));
    const std::string logFile = Logger::getLogFilePath();
    static_cast<void>(Logger::shutdown());

    const std::string contents = readWholeFile(context, pathFromText(logFile));
    context.expectContains("truncation suffix written", contents, "[truncated]");
    context.expectTrue("UTF-8 prefix retained", contents.find("prefix ") != std::string::npos);
    context.expectContains("strict formatted truncation suffix written", contents, "formatted... [truncated]");
    context.expectEq(
        "Logger-owned truncation preserves valid UTF-8",
        GameWIP::Unicode::Utf8::validate(contents).outcome,
        GameWIP::Unicode::Types::ValidationOutcome::Valid);
}
