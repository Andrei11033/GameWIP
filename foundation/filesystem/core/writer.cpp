/// @file writer.cpp
/// @brief FileSystem write-only file-handle operations.

#include "filesystem/internal/helpers/handles.h"
#include "filesystem/internal/platform.h"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Internal;

    namespace
    {
        // Open-option validation

        [[nodiscard]] bool isValidFileWriterMode(Types::File::WriterMode mode) noexcept
        {
            switch(mode)
            {
            case Types::File::WriterMode::CreateNew:
            case Types::File::WriterMode::CreateOrTruncate:
            case Types::File::WriterMode::TruncateExisting:
            case Types::File::WriterMode::OpenOrCreate:
            case Types::File::WriterMode::AppendOrCreate:
            case Types::File::WriterMode::AppendExisting:
                return true;
            }

            return false;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// File writer
    /// ------------------------------------------------------------

    FileWriter::FileWriter() noexcept = default;

    FileWriter::FileWriter(FileWriter &&other) noexcept
        : state_(std::move(other.state_))
    {
    }

    FileWriter::~FileWriter() noexcept
    {
        static_cast<void>(close());
    }

    GameWIP::Status::Types::Result<void> FileWriter::open(const Types::Path &path, const Types::File::WriterOpenOptions &options) noexcept
    {
        if(isOpen())
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::AlreadyOpen));
        }

        if(!isValidFileWriterMode(options.mode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
           !IO::isValidFlushMode(options.flushOnClose))
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
        }

        // Validate or create the parent before opening the writer.
        auto parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);

        if(!parentStatus.ok())
        {
            return parentStatus;
        }

        std::unique_ptr<Internal::FileState> newState;
        auto result = Platform::openWriter(newState, path, options);

        if(result.ok())
        {
            state_ = std::move(newState);
        }

        return result;
    }

    bool FileWriter::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool FileWriter::canSeek() const noexcept
    {
        return isOpen() && !state_->appendMode;
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress> FileWriter::write(std::span<const std::byte> bytes) noexcept
    {
        if(!isOpen())
        {
            return writeFailure(ErrorCode::NotOpen);
        }

        return Platform::writeFile(*state_, bytes);
    }

    GameWIP::Status::Types::Result<void> FileWriter::flush(IO::Types::FlushMode mode) noexcept
    {
        if(!isOpen())
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        return Platform::flushFile(*state_, mode);
    }

    GameWIP::Status::Types::Result<void> FileWriter::close() noexcept
    {
        return closeFileState(state_);
    }

    GameWIP::Status::Types::Result<std::uint64_t> FileWriter::position() const noexcept
    {
        if(!isOpen())
        {
            return GameWIP::Status::Types::Result<std::uint64_t>::failure(GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        return Platform::filePosition(*state_);
    }

    GameWIP::Status::Types::Result<std::uint64_t> FileWriter::size() const noexcept
    {
        if(!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Platform::fileSize(*state_);
    }

    GameWIP::Status::Types::Result<void> FileWriter::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if(!isOpen())
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::NotOpen));
        }

        return Platform::seekFile(*state_, offset, origin);
    }

    GameWIP::Status::Types::Result<FileLock> FileWriter::tryLockExclusive() noexcept
    {
        return tryLockFileState(
            state_,
            Types::Lock::Mode::Exclusive,
            [](auto state, Types::Lock::Mode mode)
            {
                return FileLock(std::move(state), mode);
            });
    }
} // namespace GameWIP::FileSystem
