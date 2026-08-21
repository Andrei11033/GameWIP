/// @file filters_test.inl
/// @brief Logger source and level filter correctness and concurrency suites.

void testFilterStatusesAndConcurrency(TestContext &context, const LoggerTestOptions &options)
{
    ScopedLoggerShutdown shutdown;
    std::array sources{Logger::defineSource(TestSource::Core, "Core"), Logger::defineSource(TestSource::Render, "Render")};
    OwnedLoggerConfig config = makeFileConfig(context, "filters");
    config.sources = sources;
    expectStarted(context, "filter init", Logger::init(config.ready()));

    context.expectTrue("set source filter status", Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Render), false).ok());
    context.expectFalse("render filtered", Logger::shouldLog(Logger::Types::Level::Info, TestSource::Render));
    context.expectEq(
        "unknown source filter NotFound",
        Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Unknown), false).code,
        IO::Types::ErrorCode::NotFound);
    context.expectEq(
        "invalid level filter InvalidArgument",
        Logger::setLevelFilter(static_cast<Logger::Types::Level>(99), true).code,
        IO::Types::ErrorCode::InvalidArgument);
    context.expectTrue("reset source filters status", Logger::resetSourceFilters().ok());
    context.expectTrue("set level filter status", Logger::setLevelFilter(Logger::Types::Level::Debug, false).ok());
    context.expectFalse("debug filtered", Logger::shouldLog(Logger::Types::Level::Debug));
    context.expectTrue("reset level filters status", Logger::resetLevelFilters().ok());

    if (!options.enableStressTests)
    {
        context.pass("filter concurrency skipped by LoggerTestOptions");
        return;
    }

    const std::size_t iterations = std::max<std::size_t>(500, options.stressIterationsPerThread);
    std::atomic<bool> start{false};
    std::atomic<bool> mutationsOk{true};
    std::thread producer(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (std::size_t i = 0; i < iterations; ++i)
                Logger::info(TestSource::Core, "producer {}", i);
        });
    std::thread filter(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (std::size_t i = 0; i < iterations; ++i)
            {
                if (!Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Core), (i & 1u) == 0).ok())
                    mutationsOk.store(false, std::memory_order_relaxed);
            }
        });
    start.store(true, std::memory_order_release);
    producer.join();
    filter.join();
    context.expectTrue("concurrent filter mutations succeed", mutationsOk.load(std::memory_order_relaxed));
    static_cast<void>(Logger::setSourceFilter(static_cast<Logger::Types::SourceId>(TestSource::Core), true));
    context.expectTrue("concurrent filter final flush", flushCompleted(Logger::flush(5s)));
}
