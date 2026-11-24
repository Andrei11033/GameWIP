/// @file test_hooks.h
/// @brief Win32 FileSystem test hooks.

#pragma once

#include <chrono>

#ifndef FILESYSTEM_INTERNAL_TEST_HOOKS
#define FILESYSTEM_INTERNAL_TEST_HOOKS 0
#endif

#if FILESYSTEM_INTERNAL_TEST_HOOKS
namespace GameWIP::FileSystem::Internal::TestHooks::Win32
{
    /// ------------------------------------------------------------
    /// Move synchronization
    /// ------------------------------------------------------------

    /// @brief Pauses a move after destination validation and before native commit.
    void armMoveDestinationValidatedPause() noexcept;

    /// @brief Pauses a move after native commit and before the backend returns.
    void armMoveCommittedPause() noexcept;

    /// @brief Waits until an armed move pause is reached.
    [[nodiscard]] bool waitForMovePause(std::chrono::milliseconds timeout) noexcept;

    /// @brief Releases an armed move pause.
    void releaseMovePause() noexcept;

    /// ------------------------------------------------------------
    /// File lock failure
    /// ------------------------------------------------------------

    /// @brief Forces FileLock release attempts to fail until disabled or reset.
    void setFileUnlockFailure(bool enabled) noexcept;

    /// ------------------------------------------------------------
    /// Hook state reset
    /// ------------------------------------------------------------

    /// @brief Restores all retained FileSystem test-hook state.
    void reset() noexcept;
} // namespace GameWIP::FileSystem::Internal::TestHooks::Win32
#endif
