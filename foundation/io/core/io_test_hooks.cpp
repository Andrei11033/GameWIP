/// @file io_test_hooks.cpp
/// @brief Implements source-tree-only deterministic IO failure injection.

#include "io/internal/io_test_hooks.h"

#if IO_INTERNAL_TEST_HOOKS

#include <atomic>
#include <new>
#include <stdexcept>

namespace
{
    constexpr std::uint16_t encode(GameWIP::IO::TestHooks::FailurePoint point, GameWIP::IO::TestHooks::FailureKind kind) noexcept
    {
        return static_cast<std::uint16_t>(static_cast<std::uint8_t>(point)) |
               static_cast<std::uint16_t>(static_cast<std::uint16_t>(static_cast<std::uint8_t>(kind)) << 8U);
    }

    std::atomic<std::uint16_t> pendingFailure{0};
} // namespace

namespace GameWIP::IO::TestHooks
{
    void forceNextFailure(FailurePoint point, FailureKind kind) noexcept
    {
        pendingFailure.store(encode(point, kind), std::memory_order_release);
    }

    void reset() noexcept
    {
        pendingFailure.store(0, std::memory_order_release);
    }
} // namespace GameWIP::IO::TestHooks

namespace GameWIP::IO::Detail::TestHooks
{
    void throwIfArmed(IO::TestHooks::FailurePoint point)
    {
        std::uint16_t encoded = pendingFailure.load(std::memory_order_acquire);
        if ((encoded & 0xffU) != static_cast<std::uint16_t>(point) || !pendingFailure.compare_exchange_strong(encoded, 0, std::memory_order_acq_rel))
        {
            return;
        }

        switch (static_cast<IO::TestHooks::FailureKind>((encoded >> 8U) & 0xffU))
        {
        case IO::TestHooks::FailureKind::OutOfMemory:
            throw std::bad_alloc{};
        case IO::TestHooks::FailureKind::LengthError:
            throw std::length_error("injected IO length failure");
        case IO::TestHooks::FailureKind::Unexpected:
            throw std::runtime_error("injected IO implementation failure");
        case IO::TestHooks::FailureKind::None:
            return;
        }
    }
} // namespace GameWIP::IO::Detail::TestHooks

#endif
