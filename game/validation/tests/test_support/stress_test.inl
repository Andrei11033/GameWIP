/// @file stress_test.inl
/// @brief Private TestSupport correctness cases grouped by behavioral responsibility.

    /// @brief Verifies worker coordination, exception propagation, and bounded wait helpers.
    void testStressHelpers(TestSupport::Context &context, const TestSupportTestOptions &options)
    {
        if (!options.enableStressTests)
        {
            context.skip("TestSupport stress helper tests", "disabled by TestSupportTestOptions");
            return;
        }

        TestSupport::StartGate gate;
        TestSupport::StopFlag stopFlag;
        std::atomic<std::size_t> started{0};
        std::atomic<std::size_t> stopped{0};

        std::thread opener(
            [&gate]
            {
                std::this_thread::sleep_for(10ms);
                gate.open();
            });

        TestSupport::runWorkers(
            4,
            [&](std::size_t)
            {
                gate.wait();
                const std::size_t currentStarted = started.fetch_add(1, std::memory_order_relaxed) + 1;
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

        opener.join();
        static_cast<void>(context.expectEq("StartGate releases workers", std::size_t{4}, started.load(std::memory_order_relaxed)));
        static_cast<void>(context.expectTrue("StopFlag records stop request", stopFlag.stopRequested()));
        static_cast<void>(context.expectEq("runWorkers joins all workers", std::size_t{4}, stopped.load(std::memory_order_relaxed)));

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
