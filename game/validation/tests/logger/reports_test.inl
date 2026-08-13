/// @file reports_test.inl
/// @brief Logger synchronous report, flush, and fatal-channel correctness suites.

void testReportsAndHealth(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "reports", Logger::Types::Level::Fatal);
    config.enableDebugOutput = false;
    config.enableFatalPopup = false;
    expectStarted(context, "report init", Logger::init(config.ready()));

    context.expectFalse("normal Info remains filtered", Logger::shouldLog(Logger::Types::Level::Info));
    const Logger::Types::Report::Result plain = Logger::reportError(testSource, "plain emergency report");
    context.expectTrue("emergency report delivered", reportDelivered(plain));
    context.expectEq("emergency report complete delivery", plain.delivery, Logger::Types::Report::Delivery::Complete);
    const Logger::Types::Report::Result formatted = Logger::report(Logger::Types::Level::Warn, testSource, "value {}", 17);
    context.expectTrue("formatted emergency report delivered", reportDelivered(formatted));
    const Logger::Types::Report::Result enumFormatted = Logger::report(Logger::Types::Level::Warn, TestSource::Core, "enum value {}", 23);
    context.expectTrue("enum-source formatted emergency report delivered", reportDelivered(enumFormatted));
    const Logger::Types::Report::Result enumRuntime = Logger::reportError(TestSource::Core, 2s, Logger::runtimeFormat("enum runtime {}"), 29);
    context.expectTrue("enum-source runtime emergency report delivered", reportDelivered(enumRuntime));

    const Logger::Types::Report::Result invalidTimeout = Logger::reportError(testSource, -1ms, "invalid timeout");
    context.expectEq("negative report timeout invalid argument", invalidTimeout.status.code, IO::Types::ErrorCode::InvalidArgument);
    const std::string invalidUtf8{"\xC3\x28", 2};
    context.expectEq(
        "invalid report source UTF-8 rejected",
        Logger::reportError(invalidUtf8, "message").status.code,
        IO::Types::ErrorCode::EncodingFailed);
    context.expectEq(
        "invalid report message UTF-8 rejected",
        Logger::reportError(testSource, invalidUtf8).status.code,
        IO::Types::ErrorCode::EncodingFailed);

#if LOGGER_INTERNAL_TEST_HOOKS
    GameWIP::Logger::TestHooks::forceNextTimedFlushTimeout();
    const Logger::Types::Report::Result timedOut = Logger::reportError(testSource, 2s, "delivered before report timeout");
    context.expectTrue("report timeout is not IO failure", timedOut.status.ok());
    context.expectEq("report timeout has domain outcome", timedOut.outcome, Logger::Types::Report::Outcome::TimedOut);
    context.expectEq("report timeout preserves delivery", timedOut.delivery, Logger::Types::Report::Delivery::Complete);

    GameWIP::Logger::TestHooks::forceNextFileFlushFailure();
    const Logger::Types::Report::Result flushFailed = Logger::reportError(testSource, "delivered before flush failure");
    context.expectFalse("report surfaces post-delivery flush failure", flushFailed.status.ok());
    context.expectEq("report flush failure completes attempt", flushFailed.outcome, Logger::Types::Report::Outcome::Completed);
    context.expectEq("report flush failure preserves complete delivery", flushFailed.delivery, Logger::Types::Report::Delivery::Complete);

    static_cast<void>(Logger::shutdown());
    config.output = Logger::Types::OutputMode::Both;
    expectStarted(context, "report partial-delivery reinit", Logger::init(config.ready()));
    GameWIP::Logger::TestHooks::forceNextFileWriteFailure();
    const Logger::Types::Report::Result failed = Logger::reportError(testSource, "forced file write failure");
    context.expectFalse("report surfaces sink failure", failed.status.ok());
    context.expectEq("remaining console sink gives partial delivery", failed.delivery, Logger::Types::Report::Delivery::Partial);
    const Logger::Types::Health::Snapshot health = Logger::getHealth();
    context.expectEq("file failure degrades only failed sink", health.state, Logger::Types::Health::State::Degraded);
    context.expectEq("file failure health source", health.lastFailureSource, Logger::Types::Health::FailureSource::File);
    context.expectEq("file failure preserves console output", health.effectiveOutput, Logger::Types::OutputMode::Console);
#endif
}

void testReportDoesNotDrainAsyncBacklog(TestContext &context)
{
#if LOGGER_INTERNAL_TEST_HOOKS
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "report-no-drain");
    config.workerBatchSize = 1;
    expectStarted(context, "report no-drain init", Logger::init(config.ready()));

    GameWIP::Logger::TestHooks::armWorkerDeliveryPause();
    Logger::info(testSource, "older queued line deliberately held");
    GameWIP::Logger::TestHooks::waitForWorkerDeliveryPause();

    const auto start = Clock::now();
    const Logger::Types::Report::Result report = Logger::reportError(testSource, 250ms, "emergency line bypasses old queue");
    const auto elapsed = Clock::now() - start;
    context.expectTrue("timed report completes with older queue blocked", reportDelivered(report));
    context.expectTrue("timed report does not wait for older queue", elapsed < 1s);

    GameWIP::Logger::TestHooks::releaseWorkerDeliveryPause();
    context.expectTrue("report no-drain final flush", flushCompleted(Logger::flush(2s)));
#else
    context.pass("report no-drain hook test skipped because LOGGER_INTERNAL_TEST_HOOKS=0");
#endif
}

void testFlushContracts(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "flush-contracts");
    expectStarted(context, "flush contract init", Logger::init(config.ready()));
    Logger::info(testSource, "flush me");

    context.expectTrue("indefinite flush completes", flushCompleted(Logger::flush()));
    const Logger::Types::FlushResult invalid = Logger::flush(-1ms);
    context.expectEq("negative flush timeout invalid", invalid.status.code, IO::Types::ErrorCode::InvalidArgument);

#if LOGGER_INTERNAL_TEST_HOOKS
    GameWIP::Logger::TestHooks::forceNextTimedFlushTimeout();
    const Logger::Types::FlushResult timedOut = Logger::flush(2s);
    context.expectTrue("timeout is not IO failure", timedOut.status.ok());
    context.expectEq("timeout has domain outcome", timedOut.outcome, Logger::Types::FlushOutcome::TimedOut);

    GameWIP::Logger::TestHooks::forceNextFileFlushFailure();
    const Logger::Types::FlushResult failed = Logger::flush(2s);
    context.expectFalse("flush sink failure is IO failure", failed.status.ok());
    context.expectEq("flush sink failure still completed attempt", failed.outcome, Logger::Types::FlushOutcome::Completed);
    context.expectEq("flush failure health source", Logger::getHealth().lastFailureSource, Logger::Types::Health::FailureSource::File);
#endif
}

void testFatalPopupFailure(TestContext &context)
{
#if LOGGER_INTERNAL_TEST_HOOKS
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "fatal-popup");
    config.enableFatalPopup = true;
    expectStarted(context, "fatal popup init", Logger::init(config.ready()));

    GameWIP::Logger::TestHooks::forceNextFatalPopupFailure();
    const Logger::Types::Report::Result result = Logger::reportFatal(testSource, "forced popup failure");
    context.expectFalse("fatal popup failure returned directly", result.status.ok());
    context.expectEq("fatal popup partial delivery", result.delivery, Logger::Types::Report::Delivery::Partial);
    context.expectEq("fatal popup health source", Logger::getHealth().lastFailureSource, Logger::Types::Health::FailureSource::FatalPopup);
#else
    context.pass("fatal popup failure hook test skipped because LOGGER_INTERNAL_TEST_HOOKS=0");
#endif
}

