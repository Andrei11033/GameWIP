/// @file file.cpp
/// @brief FileSystem read/write file-handle operations.

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

        [[nodiscard]] bool isValidFileAccess(Types::File::Access access) noexcept
        {
            switch(access)
            {
            case Types::File::Access::Read:
            case Types::File::Access::Write:
            case Types::File::Access::ReadWrite:
                return true;
            }

            return false;
        }

        [[nodiscard]] bool isValidFileOpenMode(Types::File::OpenMode mode) noexcept
        {
            switch(mode)
            {
            case Types::File::OpenMode::OpenExisting:
            case Types::File::OpenMode::CreateNew:
            case Types::File::OpenMode::OpenOrCreate:
            case Types::File::OpenMode::TruncateExisting:
            case Types::File::OpenMode::CreateOrTruncate:
                return true;
            }

            return false;
        }

        [[nodiscard]] bool isValidInitialPosition(Types::File::InitialPosition position) noexcept
        {
            switch(position)
            {
            case Types::File::InitialPosition::Beginning:
            case Types::File::InitialPosition::End:
                return true;
            }

            return false;
        }

        [[nodiscard]] bool opensForWrite(Types::File::Access access) noexcept
        {
            return access == Types::File::Access::Write || access == Types::File::Access::ReadWrite;
        }

        [[nodiscard]] bool modeRequiresWrite(Types::File::OpenMode mode) noexcept
        {
            switch(mode)
            {
            case Types::File::OpenMode::OpenExisting:
                return false;
            case Types::File::OpenMode::CreateNew:
            case Types::File::OpenMode::OpenOrCreate:
            case Types::File::OpenMode::TruncateExisting:
            case Types::File::OpenMode::CreateOrTruncate:
                return true;
            default:
                return false;
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// General file handle
    /// ------------------------------------------------------------

    File::File() noexcept = default;

    File::File(File &&other) noexcept
        : IO::Reader(std::move(static_cast<IO::Reader &>(other)))
        , IO::Writer(std::move(static_cast<IO::Writer &>(other)))
        , state_(std::move(other.state_))
    {
    }

    File::~File() noexcept
    {
        static_cast<void>(close());
    }

    IO::Types::Status File::open(const Types::Path &path, const Types::File::OpenOptions &options) noexcept
    {
        // Validate the requested options and their access-mode dependencies.
        if(isOpen())
        {
            return IO::makeStatus(ErrorCode::AlreadyOpen);
        }

        if(!isValidFileAccess(options.access) || !isValidFileOpenMode(options.mode) || !isValidInitialPosition(options.initialPosition) ||
           !isValidFileShare(options.share) || !isValidSymlinkPolicy(options.symlinkPolicy) || !IO::isValidFlushMode(options.flushOnClose))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        if(modeRequiresWrite(options.mode) && !opensForWrite(options.access))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        if(options.flushOnClose != IO::Types::FlushMode::None && !opensForWrite(options.access))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        // Validate or create the parent before opening the file.
        IO::Types::Status parentStatus = validateParentDirectory(path, options.createParentDirectories, options.symlinkPolicy);

        if(!parentStatus.ok())
        {
            return parentStatus;
        }

        std::unique_ptr<Internal::FileState> newState;
        IO::Types::Status status = Platform::openFile(newState, path, options);

        if(status.ok())
        {
            state_ = std::move(newState);
        }

        return status;
    }

    bool File::isOpen() const noexcept
    {
        return stateIsOpen(state_);
    }

    bool File::canSeek() const noexcept
    {
        return isOpen();
    }

    Types::File::Access File::access() const noexcept
    {
        return isOpen() ? state_->access : Types::File::Access::ReadWrite;
    }

    IO::Types::ReadResult File::read(std::span<std::byte> destination) noexcept
    {
        if(!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Platform::readFile(*state_, destination);
    }

    IO::Types::WriteResult File::write(std::span<const std::byte> bytes) noexcept
    {
        if(!isOpen())
        {
            return writeFailure(ErrorCode::NotOpen);
        }

        return Platform::writeFile(*state_, bytes);
    }

    IO::Types::Status File::flush(IO::Types::FlushMode mode) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::flushFile(*state_, mode);
    }

    IO::Types::Status File::close() noexcept
    {
        return closeFileState(state_);
    }

    IO::Types::PositionResult File::position() const noexcept
    {
        if(!isOpen())
        {
            return {.status = IO::makeStatus(ErrorCode::NotOpen)};
        }

        return Platform::filePosition(*state_);
    }

    IO::Types::SizeResult File::size() const noexcept
    {
        if(!isOpen())
        {
            return sizeFailure(ErrorCode::NotOpen);
        }

        return Platform::fileSize(*state_);
    }

    IO::Types::Status File::seek(std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::seekFile(*state_, offset, origin);
    }

    IO::Types::Status File::resize(std::uint64_t sizeBytes) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::resizeFile(*state_, sizeBytes, true);
    }

    IO::Types::Status File::resizeWithoutPositionRestore(std::uint64_t sizeBytes) noexcept
    {
        if(!isOpen())
        {
            return IO::makeStatus(ErrorCode::NotOpen);
        }

        return Platform::resizeFile(*state_, sizeBytes, false);
    }

    Types::Lock::Result File::tryLockShared() noexcept
    {
        return tryLockFileState(
            state_,
            Types::Lock::Mode::Shared,
            [](auto state, Types::Lock::Mode mode)
            {
                return FileLock(std::move(state), mode);
            });
    }

    Types::Lock::Result File::tryLockExclusive() noexcept
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
