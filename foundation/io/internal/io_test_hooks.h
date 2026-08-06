/// @file io_test_hooks.h
/// @brief Source-tree-only deterministic failure injection for IO validation.

#pragma once

#include <cstdint>

#ifndef INTERNAL_IO_TEST_HOOKS
#define INTERNAL_IO_TEST_HOOKS 0
#endif

#if INTERNAL_IO_TEST_HOOKS
namespace GameWIP::IO::TestHooks
{
    /// @brief Checked IO allocation boundary selected for one-shot failure injection.
    enum class FailurePoint : std::uint8_t
    {
        None,
        MemoryWriterWrite,
        MemoryWriterReserve,
        MemoryWriterCopyText,
        ReadAllScratchAllocation,
        ReadAllBytesStorage,
        ReadAllTextStorage
    };

    /// @brief Exception category injected inside a checked implementation boundary.
    enum class FailureKind : std::uint8_t
    {
        None,
        OutOfMemory,
        LengthError,
        Unexpected
    };

    /// @brief Arms one failure consumed by the next matching checked operation.
    void forceNextFailure(FailurePoint point, FailureKind kind) noexcept;

    /// @brief Clears every pending IO failure injection.
    void reset() noexcept;
} // namespace GameWIP::IO::TestHooks

namespace GameWIP::IO::Detail::TestHooks
{
    /// @brief Throws the armed validation exception when point matches.
    void throwIfArmed(IO::TestHooks::FailurePoint point);
} // namespace GameWIP::IO::Detail::TestHooks
#endif
