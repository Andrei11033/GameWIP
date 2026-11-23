/// @file handles.h
/// @brief Shared FileSystem handle-state operations.

#pragma once

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#include <memory>
#include <utility>

namespace GameWIP::FileSystem::Internal
{
    // Shared handle lifecycle

    [[nodiscard]] bool stateIsOpen(const std::unique_ptr<FileState> &state) noexcept;
    [[nodiscard]] IO::Types::Status closeFileState(std::unique_ptr<FileState> &state) noexcept;

    // Shared lock acquisition

    template <typename LockFactory>
    [[nodiscard]] Types::Lock::Result tryLockFileState(
        const std::unique_ptr<FileState> &state,
        Types::Lock::Mode mode,
        LockFactory &&makeLock) noexcept
    {
        if(!stateIsOpen(state))
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        Platform::NativeLockResult result = Platform::tryLockFile(*state, mode);

        if(!result.status.ok() || result.outcome == Types::Lock::Outcome::WouldBlock)
        {
            return {.status = std::move(result.status), .outcome = result.outcome};
        }

        return {.status = IO::successStatus(), .outcome = Types::Lock::Outcome::Acquired, .lock = makeLock(std::move(result.state), mode)};
    }
} // namespace GameWIP::FileSystem::Internal
