/// @file stress_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

/// @brief Observes a closed gate before release; the parent bounds any synchronization hang.
int runStartGateChild()
{
    TestSupport::Types::Reporting::Options reportOptions;
    reportOptions.writeReport = false;
    TestSupport::Runner runner(reportOptions);
    runner.runSuite(
        "StartGate child",
        [](TestSupport::Context &context)
        {
            TestSupport::StartGate gate;
            TestSupport::StopFlag stopFlag;
            std::mutex mutex;
            std::condition_variable condition;
            std::size_t approaching = 0;
            std::atomic<std::size_t> started{0};
            std::atomic<std::size_t> stopped{0};

            std::jthread workers(
                [&]
                {
                    TestSupport::runWorkers(
                        4,
                        [&](std::size_t)
                        {
                            {
                                std::lock_guard lock(mutex);
                                ++approaching;
                            }
                            condition.notify_all();
                            gate.wait();
                            const std::size_t currentStarted = started.fetch_add(1, std::memory_order_relaxed) + 1;
                            condition.notify_all();
                            if (currentStarted == 4)
                            {
                                stopFlag.requestStop();
                            }
                            while (!stopFlag.stopRequested())
                            {
                                std::this_thread::yield();
                            }
                            stopped.fetch_add(1, std::memory_order_relaxed);
                        });
                });
            // Release before the worker owner joins, including exception unwinding.
            struct ScopedGateRelease
            {
                TestSupport::StartGate &gate;
                ~ScopedGateRelease()
                {
                    gate.open();
                }
            } release{gate};
            {
                std::unique_lock lock(mutex);
                const bool ready = condition.wait_for(
                    lock,
                    5s,
                    [&]
                    {
                        return approaching == 4;
                    });
                static_cast<void>(context.expectTrue("StartGate workers reached the wait boundary", ready));
                if (ready)
                {
                    // A bounded negative observation is needed to test a blocking contract.
                    const bool passedClosedGate = condition.wait_for(
                        lock,
                        100ms,
                        [&]
                        {
                            return started.load(std::memory_order_relaxed) != 0;
                        });
                    static_cast<void>(context.expectFalse("StartGate blocks before open", passedClosedGate));
                }
            }

            gate.open();
            gate.open();
            workers.join();
            static_cast<void>(context.expectEq("StartGate releases workers", std::size_t{4}, started.load(std::memory_order_relaxed)));
            static_cast<void>(context.expectTrue("StopFlag records stop request", stopFlag.stopRequested()));
            static_cast<void>(context.expectEq("runWorkers joins all workers", std::size_t{4}, stopped.load(std::memory_order_relaxed)));
            std::atomic<std::size_t> lateWaiters{0};
            TestSupport::runWorkers(
                4,
                [&]
                {
                    gate.wait();
                    lateWaiters.fetch_add(1, std::memory_order_relaxed);
                });
            static_cast<void>(
                context.expectEq("StartGate remains open for future waiters", std::size_t{4}, lateWaiters.load(std::memory_order_relaxed)));
        });
    return GameWIP::Test::RunnerHelpers::finalizeRunner(runner);
}

/// @brief Verifies worker coordination, callable variants, and exception propagation.
void testStressHelpers(TestSupport::Context &context, std::string_view executablePath, const TestSupportTestOptions &options)
{
    if (!options.enableStressTests)
    {
        context.skip("TestSupport stress helper tests", "disabled by TestSupportTestOptions");
        return;
    }
    if (options.enableChildProcessTests)
    {
        TestSupport::Types::Process::Options childOptions;
        childOptions.executablePath = std::filesystem::path(executablePath);
        childOptions.arguments = {"--test-support-test-child=start-gate"};
        childOptions.waitTimeout = 30s;
        const auto child = TestSupport::runChildProcess(childOptions);
        if (!child.status.ok() || child.outcome != TestSupport::Types::Process::Outcome::Exited || child.exitCode != 0)
        {
            context.fail(
                "StartGate blocking, release and future waiters",
                std::format(
                    "{} outcome={} exit={}\n{}",
                    TestSupport::formatInfrastructureStatus(child.status),
                    static_cast<int>(child.outcome),
                    child.exitCode,
                    child.outputBytes));
        }
        else
        {
            context.pass("StartGate blocking, release and future waiters");
        }
    }
    else
    {
        context.skip("StartGate child", "child process tests disabled by TestSupportTestOptions");
    }
    TestSupport::StopFlag stopFlag;
    static_cast<void>(context.expectFalse("StopFlag starts without a request", stopFlag.stopRequested()));
    stopFlag.requestStop();
    stopFlag.requestStop();
    static_cast<void>(context.expectTrue("StopFlag stop request is sticky and idempotent", stopFlag.stopRequested()));

    std::atomic<std::size_t> calls{0};
    TestSupport::runWorkers(
        0,
        [&]
        {
            calls.fetch_add(1, std::memory_order_relaxed);
        });
    static_cast<void>(context.expectEq("runWorkers zero workers never invokes the callable", std::size_t{0}, calls.load(std::memory_order_relaxed)));
    TestSupport::runWorkers(
        3,
        [&]
        {
            calls.fetch_add(1, std::memory_order_relaxed);
        });
    static_cast<void>(context.expectEq("runWorkers accepts a no-argument callable", std::size_t{3}, calls.load(std::memory_order_relaxed)));

    /// @brief Assigns each copied worker callable a distinct id and records executed copies.
    struct WorkerCopyRecorder
    {
        std::shared_ptr<std::atomic<std::size_t>> nextId;
        std::shared_ptr<std::mutex> mutex;
        std::shared_ptr<std::set<std::size_t>> ids;
        std::size_t id;

        WorkerCopyRecorder(
            std::shared_ptr<std::atomic<std::size_t>> nextIdIn,
            std::shared_ptr<std::mutex> mutexIn,
            std::shared_ptr<std::set<std::size_t>> idsIn)
            : nextId(std::move(nextIdIn))
            , mutex(std::move(mutexIn))
            , ids(std::move(idsIn))
            , id(nextId->fetch_add(1, std::memory_order_relaxed) + 1)
        {
        }

        WorkerCopyRecorder(const WorkerCopyRecorder &other)
            : nextId(other.nextId)
            , mutex(other.mutex)
            , ids(other.ids)
            , id(nextId->fetch_add(1, std::memory_order_relaxed) + 1)
        {
        }

        WorkerCopyRecorder(WorkerCopyRecorder &&) noexcept = default;
        WorkerCopyRecorder &operator=(const WorkerCopyRecorder &) = delete;
        WorkerCopyRecorder &operator=(WorkerCopyRecorder &&) noexcept = delete;

        /// @brief Records the identity of this per-worker callable copy.
        void operator()(std::size_t)
        {
            std::lock_guard lock(*mutex);
            ids->insert(id);
        }
    };

    auto nextWorkerId = std::make_shared<std::atomic<std::size_t>>(0);
    auto addressMutex = std::make_shared<std::mutex>();
    auto workerIds = std::make_shared<std::set<std::size_t>>();
    TestSupport::runWorkers(4, WorkerCopyRecorder{nextWorkerId, addressMutex, workerIds});
    static_cast<void>(context.expectEq("runWorkers gives each worker its own callable copy", std::size_t{4}, workerIds->size()));

    std::atomic<std::size_t> enteredFailureWorkers{0};
    std::atomic<std::size_t> completedFailureWorkers{0};
    bool rethrown = false;
    try
    {
        TestSupport::runWorkers(
            4,
            [&](std::size_t workerIndex)
            {
                enteredFailureWorkers.fetch_add(1, std::memory_order_relaxed);
                if (workerIndex == 2)
                {
                    throw std::runtime_error("intentional worker failure");
                }
                completedFailureWorkers.fetch_add(1, std::memory_order_relaxed);
            });
    }
    catch (const std::runtime_error &)
    {
        rethrown = true;
    }

    static_cast<void>(context.expectTrue("runWorkers rethrows worker failure", rethrown));
    static_cast<void>(
        context.expectEq("runWorkers starts every failure-path worker", std::size_t{4}, enteredFailureWorkers.load(std::memory_order_relaxed)));
    static_cast<void>(context.expectEq(
        "runWorkers joins non-throwing failure-path workers",
        std::size_t{3},
        completedFailureWorkers.load(std::memory_order_relaxed)));
}
