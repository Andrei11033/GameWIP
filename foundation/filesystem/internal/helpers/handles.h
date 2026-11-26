/// @file handles.h
/// @brief Shared FileSystem handle-state operations.

#pragma once

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace GameWIP::FileSystem::Internal
{
    // Shared handle lifecycle

    [[nodiscard]] bool stateIsOpen(const std::unique_ptr<FileState> &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<void> closeFileState(std::unique_ptr<FileState> &state) noexcept;

    // Shared lock acquisition

    template <typename LockFactory>
    [[nodiscard]] auto tryLockFileState(
        const std::unique_ptr<FileState> &state,
        Types::Lock::Mode mode,
        LockFactory &&makeLock) noexcept
    {
        using Lock = std::invoke_result_t<LockFactory, std::unique_ptr<FileLockState>, Types::Lock::Mode>;
        using Result = GameWIP::Status::Types::Result<Lock>;

        if(!stateIsOpen(state))
        {
            return Result::failure(GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        auto result = Platform::tryLockFile(*state, mode);

        if(!result.ok())
        {
            return Result::failure(result.status());
        }

        return Result::success(makeLock(std::move(result).value(), mode));
    }
} // namespace GameWIP::FileSystem::Internal
