/// @file stress.cpp
/// @brief Synchronization helper implementation for TestSupport.

#include "test_support/stress.h"

namespace GameWIP::TestSupport
{
    void StartGate::wait()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(
            lock,
            [this]
            {
                return open_;
            });
    }

    void StartGate::open()
    {
        {
            std::lock_guard lock(mutex_);
            open_ = true;
        }
        condition_.notify_all();
    }

    void StopFlag::requestStop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
    }

    bool StopFlag::stopRequested() const noexcept
    {
        return stopRequested_.load(std::memory_order_acquire);
    }
} // namespace GameWIP::TestSupport
