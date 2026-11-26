/// @file files.cpp
/// @brief Win32 FileSystem file and lock operations.

#include "filesystem/internal/platform.h"
#include "filesystem/platform/win32/internal/helpers.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem::Platform
{
    using namespace Internal;
    using namespace Win32;

    /// ------------------------------------------------------------
    /// File handles and transfers
    /// ------------------------------------------------------------

    Result<void> openReader(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const Types::File::ReaderOpenOptions &options) noexcept
    {
        try
        {
            const NativeOpenRequest request{
                .desiredAccess = FILE_GENERIC_READ,
                .creationDisposition = OPEN_EXISTING,
                .flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                .access = Types::File::Access::Read,
                .flushOnClose = IO::Types::FlushMode::None,
                .share = options.share,
                .symlinkPolicy = options.symlinkPolicy,
                .existingEntryRule = ExistingEntryRule::MustExistRegularFile,
                .readable = true,
                .writable = false,
                .appendMode = false};
            return openNativeFile(state, path, request);
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    Result<void> openWriter(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const Types::File::WriterOpenOptions &options) noexcept
    {
        try
        {
            DWORD creationDisposition = CREATE_ALWAYS;
            bool appendMode = false;
            ExistingEntryRule existingRule = ExistingEntryRule::MayCreateRegularFile;

            switch(options.mode)
            {
            case Types::File::WriterMode::CreateNew:
                creationDisposition = CREATE_NEW;
                break;
            case Types::File::WriterMode::CreateOrTruncate:
                creationDisposition = CREATE_ALWAYS;
                break;
            case Types::File::WriterMode::TruncateExisting:
                creationDisposition = TRUNCATE_EXISTING;
                existingRule = ExistingEntryRule::MustExistRegularFile;
                break;
            case Types::File::WriterMode::OpenOrCreate:
                creationDisposition = OPEN_ALWAYS;
                break;
            case Types::File::WriterMode::AppendOrCreate:
                creationDisposition = OPEN_ALWAYS;
                appendMode = true;
                break;
            case Types::File::WriterMode::AppendExisting:
                creationDisposition = OPEN_EXISTING;
                existingRule = ExistingEntryRule::MustExistRegularFile;
                appendMode = true;
                break;
            default:
                return failure<void>(ErrorCode::InvalidArgument);
            }

            const NativeOpenRequest request{
                .desiredAccess = appendMode ? static_cast<DWORD>(FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE)
                                            : static_cast<DWORD>(FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES),
                .creationDisposition = creationDisposition,
                .flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                .access = Types::File::Access::Write,
                .flushOnClose = options.flushOnClose,
                .share = options.share,
                .symlinkPolicy = options.symlinkPolicy,
                .existingEntryRule = existingRule,
                .readable = false,
                .writable = true,
                .appendMode = appendMode};
            return openNativeFile(state, path, request);
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    Result<void> openFile(std::unique_ptr<Internal::FileState> &state, const Types::Path &path, const Types::File::OpenOptions &options) noexcept
    {
        try
        {
            DWORD creationDisposition = OPEN_EXISTING;
            ExistingEntryRule existingRule = ExistingEntryRule::MustExistRegularFile;

            switch(options.mode)
            {
            case Types::File::OpenMode::OpenExisting:
                creationDisposition = OPEN_EXISTING;
                break;
            case Types::File::OpenMode::CreateNew:
                creationDisposition = CREATE_NEW;
                existingRule = ExistingEntryRule::MayCreateRegularFile;
                break;
            case Types::File::OpenMode::OpenOrCreate:
                creationDisposition = OPEN_ALWAYS;
                existingRule = ExistingEntryRule::MayCreateRegularFile;
                break;
            case Types::File::OpenMode::TruncateExisting:
                creationDisposition = TRUNCATE_EXISTING;
                break;
            case Types::File::OpenMode::CreateOrTruncate:
                creationDisposition = CREATE_ALWAYS;
                existingRule = ExistingEntryRule::MayCreateRegularFile;
                break;
            default:
                return failure<void>(ErrorCode::InvalidArgument);
            }

            DWORD desiredAccess = 0;
            bool readable = false;
            bool writable = false;

            switch(options.access)
            {
            case Types::File::Access::Read:
                desiredAccess = FILE_GENERIC_READ;
                readable = true;
                break;
            case Types::File::Access::Write:
                desiredAccess = FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES;
                writable = true;
                break;
            case Types::File::Access::ReadWrite:
                desiredAccess = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_READ_ATTRIBUTES;
                readable = true;
                writable = true;
                break;
            default:
                return failure<void>(ErrorCode::InvalidArgument);
            }

            const NativeOpenRequest request{
                .desiredAccess = desiredAccess,
                .creationDisposition = creationDisposition,
                .flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                .access = options.access,
                .flushOnClose = options.flushOnClose,
                .share = options.share,
                .symlinkPolicy = options.symlinkPolicy,
                .existingEntryRule = existingRule,
                .readable = readable,
                .writable = writable,
                .appendMode = false};
            auto openResult = openNativeFile(state, path, request);

            if(!openResult.ok())
            {
                return openResult;
            }

            if(options.initialPosition == Types::File::InitialPosition::End)
            {
                auto seekResult = seekFile(*state, 0, IO::Types::SeekOrigin::End);

                if(!seekResult.ok())
                {
                    static_cast<void>(closeFile(*state));
                    state.reset();
                    return seekResult;
                }
            }
            else if(options.initialPosition != Types::File::InitialPosition::Beginning)
            {
                static_cast<void>(closeFile(*state));
                state.reset();
                return failure<void>(ErrorCode::InvalidArgument);
            }
            return success();
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    Result<IO::Types::ReadProgress> readFile(Internal::FileState &state, std::span<std::byte> destination) noexcept
    {
        if(!state.readable)
        {
            return failure<IO::Types::ReadProgress>(ErrorCode::PermissionDenied);
        }

        try
        {
            if(destination.empty())
            {
                auto position = filePosition(state);

                if(!position.ok())
                {
                    return Result<IO::Types::ReadProgress>::failure(position.status(), IO::Types::ReadProgress{});
                }

                auto size = fileSize(state);

                if(!size.ok())
                {
                    return Result<IO::Types::ReadProgress>::failure(size.status(), IO::Types::ReadProgress{});
                }

                return Result<IO::Types::ReadProgress>::success(
                    IO::Types::ReadProgress{.bytesRead = 0, .endOfStream = position.value() >= size.value()});
            }

            const auto requestSize =
                static_cast<DWORD>(std::min<std::size_t>(destination.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD bytesRead = 0;

            if(ReadFile(nativeHandle(state), destination.data(), requestSize, &bytesRead, nullptr) == FALSE)
            {
                return Result<IO::Types::ReadProgress>::failure(
                    makeLastErrorStatus(ErrorCode::ReadFailed), IO::Types::ReadProgress{.bytesRead = bytesRead});
            }

            return Result<IO::Types::ReadProgress>::success(
                IO::Types::ReadProgress{.bytesRead = bytesRead, .endOfStream = bytesRead < requestSize});
        }
        catch(const std::bad_alloc &)
        {
            return failure<IO::Types::ReadProgress>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<IO::Types::ReadProgress>(ErrorCode::Unknown);
        }
    }

    Result<IO::Types::WriteProgress> writeFile(Internal::FileState &state, std::span<const std::byte> bytes) noexcept
    {
        if(!state.writable)
        {
            return failure<IO::Types::WriteProgress>(ErrorCode::PermissionDenied);
        }

        try
        {
            if(bytes.empty())
            {
                return Result<IO::Types::WriteProgress>::success(IO::Types::WriteProgress{});
            }

            const auto requestSize =
                static_cast<DWORD>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD bytesWritten = 0;

            if(WriteFile(nativeHandle(state), bytes.data(), requestSize, &bytesWritten, nullptr) == FALSE)
            {
                return Result<IO::Types::WriteProgress>::failure(
                    makeLastErrorStatus(ErrorCode::WriteFailed), IO::Types::WriteProgress{.bytesWritten = bytesWritten});
            }

            return Result<IO::Types::WriteProgress>::success(IO::Types::WriteProgress{.bytesWritten = bytesWritten});
        }
        catch(const std::bad_alloc &)
        {
            return failure<IO::Types::WriteProgress>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<IO::Types::WriteProgress>(ErrorCode::Unknown);
        }
    }

    Result<void> flushFile(Internal::FileState &state, IO::Types::FlushMode mode) noexcept
    {
        try
        {
            if(!IO::isValidFlushMode(mode))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            if(mode == IO::Types::FlushMode::None)
            {
                return success();
            }

            if(FlushFileBuffers(nativeHandle(state)) == FALSE)
            {
                return failure<void>(makeLastErrorStatus(ErrorCode::FlushFailed));
            }

            return success();
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    Result<void> closeFile(Internal::FileState &state) noexcept
    {
        try
        {
            if(state.activeLocks && *state.activeLocks > 0)
            {
                return failure<void>(ErrorCode::ResourceBusy);
            }

            if(state.writable)
            {
                auto flushResult = flushFile(state, state.flushOnClose);

                if(!flushResult.ok())
                {
                    return flushResult;
                }
            }

            auto closeResult = closeNativeHandle(state.nativeHandle);

            if(closeResult.ok())
            {
                clearNativeHandle(state);
            }

            return closeResult;
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    Result<std::uint64_t> filePosition(const Internal::FileState &state) noexcept
    {
        try
        {
            if(state.appendMode)
            {
                return failure<std::uint64_t>(ErrorCode::NotSeekable);
            }

            return nativePosition(nativeHandle(state));
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::uint64_t>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::uint64_t>(ErrorCode::Unknown);
        }
    }

    Result<std::uint64_t> fileSize(const Internal::FileState &state) noexcept
    {
        try
        {
            return nativeFileSize(nativeHandle(state));
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::uint64_t>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::uint64_t>(ErrorCode::Unknown);
        }
    }

    Result<void> seekFile(Internal::FileState &state, std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        try
        {
            if(state.appendMode)
            {
                return failure<void>(ErrorCode::NotSeekable);
            }

            return seekNativeHandle(nativeHandle(state), offset, origin);
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    Result<void> resizeFile(Internal::FileState &state, std::uint64_t sizeBytes, bool restorePosition) noexcept
    {
        try
        {
            if(!state.writable)
            {
                return failure<void>(ErrorCode::PermissionDenied);
            }

            if(sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return failure<void>(ErrorCode::SizeLimitExceeded);
            }

            // Report a failed position restore even though the new length has already been committed.
            auto originalPosition = restorePosition && !state.appendMode ? filePosition(state) : Result<std::uint64_t>::success(0);

            if(restorePosition && !state.appendMode && !originalPosition.ok())
            {
                return failure<void>(originalPosition.status());
            }

            LARGE_INTEGER target{};
            target.QuadPart = static_cast<LONGLONG>(sizeBytes);

            if(SetFilePointerEx(nativeHandle(state), target, nullptr, FILE_BEGIN) == FALSE)
            {
                return failure<void>(makeLastErrorStatus(ErrorCode::ResizeFailed));
            }

            if(SetEndOfFile(nativeHandle(state)) == FALSE)
            {
                return failure<void>(makeLastErrorStatus(ErrorCode::ResizeFailed));
            }

            if(restorePosition && !state.appendMode && originalPosition.value() <= sizeBytes)
            {
                return seekNativeHandle(nativeHandle(state), static_cast<std::int64_t>(originalPosition.value()), IO::Types::SeekOrigin::Begin);
            }

            return success();
        }
        catch(const std::bad_alloc &)
        {
            return failure<void>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<void>(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// File locks
    /// ------------------------------------------------------------

    Result<std::unique_ptr<Internal::FileLockState>> tryLockFile(Internal::FileState &state, Types::Lock::Mode mode) noexcept
    {
        try
        {
            DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;

            switch(mode)
            {
            case Types::Lock::Mode::Shared:
                break;
            case Types::Lock::Mode::Exclusive:
                flags |= LOCKFILE_EXCLUSIVE_LOCK;
                break;
            default:
                return failure<std::unique_ptr<Internal::FileLockState>>(ErrorCode::InvalidArgument);
            }

            if(!state.activeLocks)
            {
                state.activeLocks = std::make_shared<std::uint32_t>(0);
            }

            auto lockState = std::make_unique<Internal::FileLockState>();

            // Acquire a detached handle before locking so every successful native lock is immediately owned by
            // the returned FileLock. This avoids an unobservable lock if duplication were to fail after locking.
            HANDLE duplicatedHandleRaw = INVALID_HANDLE_VALUE;

            if(DuplicateHandle(
                   GetCurrentProcess(),
                   nativeHandle(state),
                   GetCurrentProcess(),
                   &duplicatedHandleRaw,
                   0,
                   FALSE,
                   DUPLICATE_SAME_ACCESS) == FALSE)
            {
                return failure<std::unique_ptr<Internal::FileLockState>>(makeLastErrorStatus(ErrorCode::LockFailed));
            }

            UniqueHandle duplicatedHandle{duplicatedHandleRaw};

            OVERLAPPED overlapped{};

            if(LockFileEx(duplicatedHandle.get(), flags, 0, MAXDWORD, MAXDWORD, &overlapped) == FALSE)
            {
                const DWORD error = GetLastError();

                if(error == ERROR_LOCK_VIOLATION || error == ERROR_SHARING_VIOLATION)
                {
                    return failure<std::unique_ptr<Internal::FileLockState>>(ErrorCode::WouldBlock);
                }

                return failure<std::unique_ptr<Internal::FileLockState>>(makeWin32Status(error, ErrorCode::LockFailed));
            }

            lockState->nativeHandle = duplicatedHandle.release();
            lockState->activeLocks = state.activeLocks;
            lockState->active = true;
            lockState->exclusive = mode == Types::Lock::Mode::Exclusive;
            ++(*lockState->activeLocks);

            return Result<std::unique_ptr<Internal::FileLockState>>::success(std::move(lockState));
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::unique_ptr<Internal::FileLockState>>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::unique_ptr<Internal::FileLockState>>(ErrorCode::Unknown);
        }
    }

    Result<void> unlockFile(Internal::FileLockState &state) noexcept
    {
        return unlockNativeFile(state);
    }

} // namespace GameWIP::FileSystem::Platform
