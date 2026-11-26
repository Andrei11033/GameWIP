/// @file reader.cpp
/// @brief FileSystem read-only file-handle operations.

#include "filesystem/internal/helpers/handles.h"
#include "filesystem/internal/platform.h"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Internal;

    /// ------------------------------------------------------------
    /// File reader
    /// ------------------------------------------------------------

    FileReader::FileReader() noexcept = default;

    FileReader::FileReader(FileReader &&other) noexcept
        : state_(std::move(other.state_))
    {
    }

    FileReader::~FileReader() noexcept
    {
        static_cast<void>(close());
    }

    GameWIP::Status::Types::Result<void> FileReader::open(const Types::Path &path, const Types::File::ReaderOpenOptions &options) noexcept
    {
        if(isOpen())
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyOpen));
        }

        if(!isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
        }

        std::unique_ptr<Internal::FileState> newState;
        auto result = Platform::openReader(newState, path, options);

        if(result.ok())
        {
            state_ = std::move(newState);
        }

        return result;
    }

    bool FileReader::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool FileReader::canSeek() const noexcept
    {
        return isOpen();
    }

    GameWIP::Status::Types::Result<IO::Types::ReadProgress> FileReader::read(std::span<std::byte> destination) noexcept
    {
        if(!isOpen())
        {
            return GameWIP::Status::Types::Result<IO::Types::ReadProgress>::failure(
                GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        return Platform::readFile(*state_, destination);
    }

    GameWIP::Status::Types::Result<void> FileReader::close() noexcept
    {
        return closeFileState(state_);
    }

    GameWIP::Status::Types::Result<std::uint64_t> FileReader::position() const noexcept
    {
        if(!isOpen())
        {
            return GameWIP::Status::Types::Result<std::uint64_t>::failure(GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        return Platform::filePosition(*state_);
    }

    GameWIP::Status::Types::Result<std::uint64_t> FileReader::size() const noexcept
    {
        if(!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Platform::fileSize(*state_);
    }

    GameWIP::Status::Types::Result<void> FileReader::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if(!isOpen())
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        return Platform::seekFile(*state_, offset, origin);
    }

    GameWIP::Status::Types::Result<FileLock> FileReader::tryLockShared() noexcept
    {
        return tryLockFileState(
            state_,
            Types::Lock::Mode::Shared,
            [](auto state, Types::Lock::Mode mode)
            {
                return FileLock(std::move(state), mode);
            });
    }
} // namespace GameWIP::FileSystem
