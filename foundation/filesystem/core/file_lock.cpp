/// @file file_lock.cpp
/// @brief FileSystem whole-file lock ownership.

#include "filesystem/internal/platform.h"

#include <memory>
#include <utility>

namespace GameWIP::FileSystem
{
    /// ------------------------------------------------------------
    /// File locks
    /// ------------------------------------------------------------

    FileLock::FileLock() noexcept = default;

    FileLock::FileLock(FileLock &&other) noexcept
        : state_(std::move(other.state_))
        , mode_(other.mode_)
    {
    }

    FileLock::~FileLock() noexcept
    {
        static_cast<void>(unlock());
    }

    bool FileLock::active() const noexcept
    {
        return state_ != nullptr && state_->active;
    }

    Types::Lock::Mode FileLock::mode() const noexcept
    {
        return mode_;
    }

    IO::Types::Status FileLock::unlock() noexcept
    {
        if(!active())
        {
            state_.reset();
            return IO::successStatus();
        }

        IO::Types::Status status = Platform::unlockFile(*state_);

        if(status.ok())
        {
            state_.reset();
        }

        return status;
    }

    FileLock::FileLock(std::unique_ptr<Internal::FileLockState> state, Types::Lock::Mode mode) noexcept
        : state_(std::move(state))
        , mode_(mode)
    {
    }

} // namespace GameWIP::FileSystem
