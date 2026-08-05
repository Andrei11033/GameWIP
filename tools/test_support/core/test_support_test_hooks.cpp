/// @file test_support_test_hooks.cpp
/// @brief Storage and operations for source-tree-only TestSupport failure injection.

#include "test_support/internal/test_support_test_hooks.h"

#if INTERNAL_TEST_SUPPORT_TEST_HOOKS
#include <atomic>
#include <cstdint>

namespace
{
    std::atomic<GameWIP::TestSupport::TestHooks::ChildProcessFailurePoint> childProcessFailurePoint{
        GameWIP::TestSupport::TestHooks::ChildProcessFailurePoint::None};
    std::atomic<std::uint64_t> childProcessNativeCode{0};
    std::atomic<GameWIP::TestSupport::TestHooks::FileFailurePoint> fileFailurePoint{GameWIP::TestSupport::TestHooks::FileFailurePoint::None};
    std::atomic<std::uint64_t> fileNativeCode{0};
    std::atomic<GameWIP::TestSupport::TestHooks::EnvironmentFailurePoint> environmentFailurePoint{
        GameWIP::TestSupport::TestHooks::EnvironmentFailurePoint::None};
    std::atomic<std::uint64_t> environmentNativeCode{0};
} // namespace

namespace GameWIP::TestSupport::TestHooks
{
    void reset() noexcept
    {
        childProcessNativeCode.store(0, std::memory_order_relaxed);
        childProcessFailurePoint.store(ChildProcessFailurePoint::None, std::memory_order_release);
        fileNativeCode.store(0, std::memory_order_relaxed);
        fileFailurePoint.store(FileFailurePoint::None, std::memory_order_release);
        environmentNativeCode.store(0, std::memory_order_relaxed);
        environmentFailurePoint.store(EnvironmentFailurePoint::None, std::memory_order_release);
    }

    void forceNextFileFailure(FileFailurePoint point, std::uint64_t nativeCode) noexcept
    {
        fileNativeCode.store(nativeCode, std::memory_order_relaxed);
        fileFailurePoint.store(point, std::memory_order_release);
    }

    void forceNextEnvironmentFailure(EnvironmentFailurePoint point, std::uint64_t nativeCode) noexcept
    {
        environmentNativeCode.store(nativeCode, std::memory_order_relaxed);
        environmentFailurePoint.store(point, std::memory_order_release);
    }

    void forceNextChildProcessFailure(ChildProcessFailurePoint point, std::uint64_t nativeCode) noexcept
    {
        childProcessNativeCode.store(nativeCode, std::memory_order_relaxed);
        childProcessFailurePoint.store(point, std::memory_order_release);
    }
} // namespace GameWIP::TestSupport::TestHooks

namespace GameWIP::TestSupport::Detail::TestHooks
{
    std::optional<std::uint64_t> consumeChildProcessFailure(TestSupport::TestHooks::ChildProcessFailurePoint point) noexcept
    {
        auto expected = point;
        if (!childProcessFailurePoint.compare_exchange_strong(
                expected,
                TestSupport::TestHooks::ChildProcessFailurePoint::None,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return std::nullopt;
        }

        return childProcessNativeCode.exchange(0, std::memory_order_relaxed);
    }

    std::optional<std::uint64_t> consumeFileFailure(TestSupport::TestHooks::FileFailurePoint point) noexcept
    {
        auto expected = point;
        if (!fileFailurePoint.compare_exchange_strong(
                expected,
                TestSupport::TestHooks::FileFailurePoint::None,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return std::nullopt;
        }

        return fileNativeCode.exchange(0, std::memory_order_relaxed);
    }

    std::optional<std::uint64_t> consumeEnvironmentFailure(TestSupport::TestHooks::EnvironmentFailurePoint point) noexcept
    {
        auto expected = point;
        if (!environmentFailurePoint.compare_exchange_strong(
                expected,
                TestSupport::TestHooks::EnvironmentFailurePoint::None,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return std::nullopt;
        }

        return environmentNativeCode.exchange(0, std::memory_order_relaxed);
    }
} // namespace GameWIP::TestSupport::Detail::TestHooks
#endif
