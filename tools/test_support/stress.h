/// @file stress.h
/// @brief Small synchronization and worker helpers for stress validation.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace GameWIP::TestSupport
{
    /// @brief One-shot gate that blocks workers until opened.
    /// @note Opening is idempotent; current and future waiters pass after the gate opens and it cannot be reset.
    class StartGate
    {
    public:
        /// @brief Blocks until open() is called.
        void wait();
        /// @brief Opens the gate and releases all current and future waiters.
        void open();

    private:
        std::mutex mutex_;                  ///< Protects open_ and the wait protocol.
        std::condition_variable condition_; ///< Wakes blocked waiters when the gate opens.
        bool open_ = false;                 ///< Sticky one-way open state.
    };

    /// @brief One-way atomic cooperative-stop flag.
    class StopFlag
    {
    public:
        /// @brief Requests cooperative stop with release ordering.
        void requestStop() noexcept;
        /// @brief Returns true after requestStop() using acquire ordering.
        [[nodiscard]] bool stopRequested() const noexcept;

    private:
        std::atomic<bool> stopRequested_{false}; ///< Sticky cooperative-stop state.
    };

    /// @brief Starts workerCount threads, joins every started worker, and rethrows one captured worker failure.
    /// @tparam WorkerFunction Copy-constructible callable accepting std::size_t or no arguments.
    /// @param workerCount Number of workers; zero starts none.
    /// @param workerFunction Prototype callable copied into each worker.
    /// @note Worker exceptions do not stop peers. One scheduling-dependent exception is rethrown after every started worker is joined.
    template <typename WorkerFunction> void runWorkers(std::size_t workerCount, WorkerFunction &&workerFunction)
    {
        using Worker = std::decay_t<WorkerFunction>;
        static_assert(
            std::is_copy_constructible_v<Worker>,
            "WorkerFunction must be copy constructible so each worker can receive independent callable state.");

        if (workerCount == 0)
        {
            return;
        }

        Worker workerPrototype(std::forward<WorkerFunction>(workerFunction));
        std::mutex exceptionMutex;
        std::exception_ptr firstException;
        std::vector<std::thread> workers;
        workers.reserve(workerCount);

        try
        {
            for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
            {
                workers.emplace_back(
                    [workerIndex, worker = workerPrototype, &exceptionMutex, &firstException]() mutable
                    {
                        try
                        {
                            if constexpr (std::is_invocable_v<Worker &, std::size_t>)
                            {
                                worker(workerIndex);
                            }
                            else if constexpr (std::is_invocable_v<Worker &>)
                            {
                                worker();
                            }
                            else
                            {
                                static_assert(
                                    std::is_invocable_v<Worker &, std::size_t>,
                                    "WorkerFunction must be invocable with size_t or with no arguments.");
                            }
                        }
                        catch (...)
                        {
                            std::lock_guard lock(exceptionMutex);
                            if (!firstException)
                            {
                                firstException = std::current_exception();
                            }
                        }
                    });
            }
        }
        catch (...)
        {
            for (std::thread &worker : workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
            throw;
        }

        for (std::thread &worker : workers)
        {
            worker.join();
        }

        if (firstException)
        {
            std::rethrow_exception(firstException);
        }
    }
} // namespace GameWIP::TestSupport
