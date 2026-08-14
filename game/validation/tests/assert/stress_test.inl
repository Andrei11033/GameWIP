/// @file stress_test.inl
/// @brief Private Assert correctness cases for concurrent and repeated failure-path stress.

    /// @brief Provides one CHECK_ONCE call site shared by all stress-test threads.
    void threadedCheckOnceSite()
    {
        CHECK_ONCE_MSG(false, "threaded check once stress");
    }

    /// @brief Verifies one CHECK_ONCE site reports at most once under concurrent contention.
    void testCheckOnceThreadStress(TestContext &context, const AssertTestOptions &options)
    {
#if ASSERT_CHECKS_ENABLED
        if (!options.enableStressTests)
        {
            context.pass("CHECK_ONCE thread stress skipped by AssertTestOptions");
            return;
        }

        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "check_once_thread_stress"))
        {
            context.fail("CHECK_ONCE thread stress logger init", "Logger::init failed");
            return;
        }

        const int threadCount = static_cast<int>(std::max<std::size_t>(2, options.stressThreadCount));
        std::atomic<bool> start{false};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(threadCount));

        for (int index = 0; index < threadCount; ++index)
        {
            workers.emplace_back(
                [&start]
                {
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                    threadedCheckOnceSite();
                });
        }

        start.store(true, std::memory_order_release);
        for (std::thread &worker : workers)
        {
            worker.join();
        }

        Logger::flush(2s);
        const std::string contents = readFile(context, Logger::getLogFilePath());
#if ASSERT_DIAGNOSTICS
        context.expectEq("CHECK_ONCE thread stress logs once", countOccurrences(contents, "threaded check once stress"), std::size_t{1});
#else
        context.expectEq("CHECK_ONCE thread stress logs generic once", countOccurrences(contents, "[ERROR][Check]: Check failed"), std::size_t{1});
#endif
#else
        context.pass("CHECK_ONCE thread stress skipped because ASSERT_CHECKS_ENABLED=0");
#endif
    }

    /// @brief Verifies repeated automated interactive actions retain call-site state correctly.
    void testInteractiveStressLoops(TestContext &context, const AssertTestOptions &options)
    {
#if ASSERT_ENABLED
        if (!options.enableStressTests)
        {
            context.pass("interactive assert stress loops skipped by AssertTestOptions");
            return;
        }

        ScopedLoggerShutdown loggerShutdown;
        if (!initFileLogger(context, "interactive_stress_loops"))
        {
            context.fail("interactive stress logger init", "Logger::init failed");
            return;
        }

        const int iterations = static_cast<int>(std::max<std::size_t>(1, options.stressIterations));
        const ScopedEnvironmentVariable testAction(testActionEnvironmentVariable, "ignore_once");
        if (!requireInfrastructure(context, "set interactive stress action", testAction.status()))
        {
            return;
        }
        for (int index = 0; index < iterations; ++index)
        {
            interactiveIgnoreOnceSite();
        }

        Logger::flush(5s);
        const std::string contents = readFile(context, Logger::getLogFilePath());
#if ASSERT_DIAGNOSTICS
        context.expectEq(
            "ASSERT_INTERACTIVE ignore_once stress logs every failure",
            countOccurrences(contents, "interactive ignore once repeat test"),
            static_cast<std::size_t>(iterations));
#else
        context.expectEq(
            "ASSERT_INTERACTIVE ignore_once stress logs generic failures",
            countOccurrences(contents, "[FATAL][Assert]: Assert failed"),
            static_cast<std::size_t>(iterations));
#endif
#else
        context.pass("interactive assert stress loops skipped because ASSERT_ENABLED=0");
#endif
    }
