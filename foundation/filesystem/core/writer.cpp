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

    IO::Types::Status FileWriter::open(const Types::Path &path, const Types::File::WriterOpenOptions &options) noexcept
    {
        if(isOpen())
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }

        if(!isValidFileWriterMode(options.mode) || !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) ||
           !IO::isValidFlushMode(options.flushOnClose))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        // Validate or create the parent before opening the writer.
        IO::Types::Status parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);

        if(!parentStatus.ok())
        {
            return parentStatus;
        }

        std::unique_ptr<Internal::FileState> newState;
        IO::Types::Status status = Platform::openWriter(newState, path, options);

        if(status.ok())
        {
            state_ = std::move(newState);
        }

        return status;
    }

    bool FileWriter::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool FileWriter::canSeek() const noexcept
    {
        return isOpen() && !state_->appendMode;
    }

    IO::Types::WriteResult FileWriter::write(std::span<const std::byte> bytes) noexcept
    {
        if(!isOpen())
        {
            return writeFailure(ErrorCode::NotOpen);
        }

        return Platform::writeFile(*state_, bytes);
    }

    IO::Types::Status FileWriter::flush(IO::Types::FlushMode mode) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::flushFile(*state_, mode);
    }

    IO::Types::Status FileWriter::close() noexcept
    {
        return closeFileState(state_);
    }

    IO::Types::PositionResult FileWriter::position() const noexcept
    {
        if(!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Platform::filePosition(*state_);
    }

    IO::Types::SizeResult FileWriter::size() const noexcept
    {
        if(!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Platform::fileSize(*state_);
    }

    IO::Types::Status FileWriter::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::seekFile(*state_, offset, origin);
    }

    Types::Lock::Result FileWriter::tryLockExclusive() noexcept
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
