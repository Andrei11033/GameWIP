/// @file health_test.inl
/// @brief Logger lifecycle health and statistics correctness suites.

void testShutdownAndHealthEpoch(TestContext &context)
{
    ScopedLoggerShutdown shutdown;
    OwnedLoggerConfig config = makeFileConfig(context, "health-epoch");
    expectStarted(context, "health epoch init", Logger::init(config.ready()));

#if LOGGER_INTERNAL_TEST_HOOKS
    std::atomic<bool> observeHealth{true};
    std::atomic<bool> healthSnapshotsCoherent{true};
    std::thread healthObserver(
        [&]
        {
            while (observeHealth.load(std::memory_order_acquire))
            {
                const Logger::Types::Health::Snapshot snapshot = Logger::getHealth();
                const bool healthy = snapshot.state == Logger::Types::Health::State::Healthy &&
                                     snapshot.effectiveOutput == Logger::Types::OutputMode::File &&
                                     snapshot.lastFailureSource == Logger::Types::Health::FailureSource::None &&
                                     snapshot.lastError == IO::Types::ErrorCode::Success && snapshot.failureCount == 0;
                const bool failed = snapshot.state == Logger::Types::Health::State::Disabled &&
                                    snapshot.effectiveOutput == Logger::Types::OutputMode::None &&
                                    snapshot.lastFailureSource == Logger::Types::Health::FailureSource::File &&
                                    snapshot.lastError != IO::Types::ErrorCode::Success && snapshot.failureCount > 0;
                if (!healthy && !failed)
                    healthSnapshotsCoherent.store(false, std::memory_order_release);
            }
        });
    GameWIP::Logger::TestHooks::forceNextFileWriteFailure();
    Logger::info(testSource, "worker failure");
    static_cast<void>(Logger::flush(2s));
    observeHealth.store(false, std::memory_order_release);
    healthObserver.join();
    context.expectTrue("concurrent health snapshots remain coherent", healthSnapshotsCoherent.load(std::memory_order_acquire));
    const Logger::Types::Health::Snapshot failedHealth = Logger::getHealth();
    context.expectTrue("health records failure count", failedHealth.failureCount > 0);
    Logger::resetStats();
    const Logger::Types::Health::Snapshot healthAfterStatsReset = Logger::getHealth();
    context.expectEq("resetStats preserves health state", healthAfterStatsReset.state, failedHealth.state);
    context.expectEq("resetStats preserves health source", healthAfterStatsReset.lastFailureSource, failedHealth.lastFailureSource);
    context.expectEq("resetStats preserves health error", healthAfterStatsReset.lastError, failedHealth.lastError);
    context.expectEq("resetStats preserves health native code", healthAfterStatsReset.lastNativeCode, failedHealth.lastNativeCode);
    context.expectEq("resetStats preserves health count", healthAfterStatsReset.failureCount, failedHealth.failureCount);
#endif

    const IO::Types::Status shutdownStatus = Logger::shutdown();
    context.expectFalse("shutdown always stops Logger", Logger::isRunning());
    context.expectEq("shutdown health disabled", Logger::getHealth().state, Logger::Types::Health::State::Disabled);
    static_cast<void>(shutdownStatus);

    const Logger::Types::Init::Result reinit = Logger::init(makeConsoleConfig());
    expectStarted(context, "health epoch reinit", reinit);
    const Logger::Types::Health::Snapshot reset = Logger::getHealth();
    context.expectEq("new init resets health state", reset.state, Logger::Types::Health::State::Healthy);
    context.expectEq("new init resets failure count", reset.failureCount, std::uint64_t{0});
    context.expectEq("new init resets failure source", reset.lastFailureSource, Logger::Types::Health::FailureSource::None);
}

