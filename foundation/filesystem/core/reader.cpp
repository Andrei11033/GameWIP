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

    IO::Types::Status FileReader::open(const Types::Path &path, const Types::File::ReaderOpenOptions &options) noexcept
    {
        if(isOpen())
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }

        if(!isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        std::unique_ptr<Internal::FileState> newState;
        IO::Types::Status status = Platform::openReader(newState, path, options);

        if(status.ok())
        {
            state_ = std::move(newState);
        }

        return status;
    }

    bool FileReader::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool FileReader::canSeek() const noexcept
    {
        return isOpen();
    }

    IO::Types::ReadResult FileReader::read(std::span<std::byte> destination) noexcept
    {
        if(!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Platform::readFile(*state_, destination);
    }

    IO::Types::Status FileReader::close() noexcept
    {
        return closeFileState(state_);
    }

    IO::Types::PositionResult FileReader::position() const noexcept
    {
        if(!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Platform::filePosition(*state_);
    }

    IO::Types::SizeResult FileReader::size() const noexcept
    {
        if(!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Platform::fileSize(*state_);
    }

    IO::Types::Status FileReader::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::seekFile(*state_, offset, origin);
    }

    Types::Lock::Result FileReader::tryLockShared() noexcept
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
