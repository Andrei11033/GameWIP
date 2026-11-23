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

    IO::Types::Status openReader(
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
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status openWriter(
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
                return IO::makeStatus(ErrorCode::InvalidArgument);
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
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// File operations
    /// ------------------------------------------------------------

    IO::Types::Status openFile(std::unique_ptr<Internal::FileState> &state, const Types::Path &path, const Types::File::OpenOptions &options) noexcept
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
                return IO::makeStatus(ErrorCode::InvalidArgument);
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
                return IO::makeStatus(ErrorCode::InvalidArgument);
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
            IO::Types::Status openStatus = openNativeFile(state, path, request);

            if(!openStatus.ok())
            {
                return openStatus;
            }

            if(options.initialPosition == Types::File::InitialPosition::End)
            {
                IO::Types::Status seekStatus = seekFile(*state, 0, IO::Types::SeekOrigin::End);

                if(!seekStatus.ok())
                {
                    static_cast<void>(closeFile(*state));
                    state.reset();
                    return seekStatus;
                }
            }
            else if(options.initialPosition != Types::File::InitialPosition::Beginning)
            {
                static_cast<void>(closeFile(*state));
                state.reset();
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::ReadResult readFile(Internal::FileState &state, std::span<std::byte> destination) noexcept
    {
        if(!state.readable)
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied)};
        }

        try
        {
            if(destination.empty())
            {
                IO::Types::PositionResult position = filePosition(state);
                IO::Types::SizeResult size = fileSize(state);
                const bool atEnd = position.status.ok() && size.status.ok() && position.position >= size.sizeBytes;
                return {.status = position.status.ok() ? std::move(size.status) : std::move(position.status), .bytesRead = 0, .endOfStream = atEnd};
            }

            const auto requestSize =
                static_cast<DWORD>(std::min<std::size_t>(destination.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD bytesRead = 0;

            if(ReadFile(nativeHandle(state), destination.data(), requestSize, &bytesRead, nullptr) == FALSE)
            {
                return {.status = makeLastErrorStatus(ErrorCode::ReadFailed)};
            }

            return {.status = IO::successStatus(), .bytesRead = bytesRead, .endOfStream = bytesRead < requestSize};
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::WriteResult writeFile(Internal::FileState &state, std::span<const std::byte> bytes) noexcept
    {
        if(!state.writable)
        {
            return {.status = IO::makeStatus(ErrorCode::PermissionDenied)};
        }

        try
        {
            if(bytes.empty())
            {
                return {.status = IO::successStatus(), .bytesWritten = 0};
            }

            const auto requestSize =
                static_cast<DWORD>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD bytesWritten = 0;

            if(WriteFile(nativeHandle(state), bytes.data(), requestSize, &bytesWritten, nullptr) == FALSE)
            {
                return {.status = makeLastErrorStatus(ErrorCode::WriteFailed), .bytesWritten = bytesWritten};
            }

            return {.status = IO::successStatus(), .bytesWritten = bytesWritten};
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::Status flushFile(Internal::FileState &state, IO::Types::FlushMode mode) noexcept
    {
        try
        {
            if(!IO::isValidFlushMode(mode))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(mode == IO::Types::FlushMode::None)
            {
                return IO::successStatus();
            }

            if(FlushFileBuffers(nativeHandle(state)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::FlushFailed);
            }

            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status closeFile(Internal::FileState &state) noexcept
    {
        try
        {
            if(state.activeLocks && *state.activeLocks > 0)
            {
                return IO::makeStatus(ErrorCode::ResourceBusy);
            }

            if(state.writable)
            {
                IO::Types::Status flushStatus = flushFile(state, state.flushOnClose);

                if(!flushStatus.ok())
                {
                    return flushStatus;
                }
            }

            IO::Types::Status closeStatus = closeNativeHandle(state.nativeHandle);

            if(closeStatus.ok())
            {
                clearNativeHandle(state);
            }

            return closeStatus;
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::PositionResult filePosition(const Internal::FileState &state) noexcept
    {
        try
        {
            if(state.appendMode)
            {
                return {.status = IO::makeStatus(ErrorCode::NotSeekable)};
            }

            return nativePosition(nativeHandle(state));
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::SizeResult fileSize(const Internal::FileState &state) noexcept
    {
        try
        {
            return nativeFileSize(nativeHandle(state));
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::Status seekFile(Internal::FileState &state, std::int64_t offset, IO::Types::SeekOrigin origin) noexcept
    {
        try
        {
            if(state.appendMode)
            {
                return IO::makeStatus(ErrorCode::NotSeekable);
            }

            return seekNativeHandle(nativeHandle(state), offset, origin);
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    IO::Types::Status resizeFile(Internal::FileState &state, std::uint64_t sizeBytes, bool restorePosition) noexcept
    {
        try
        {
            if(!state.writable)
            {
                return IO::makeStatus(ErrorCode::PermissionDenied);
            }

            if(sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }

            // Report a failed position restore even though the new length has already been committed.
            const IO::Types::PositionResult originalPosition =
                restorePosition && !state.appendMode ? filePosition(state) : IO::Types::PositionResult{};

            if(restorePosition && !state.appendMode && !originalPosition.status.ok())
            {
                return originalPosition.status;
            }

            LARGE_INTEGER target{};
            target.QuadPart = static_cast<LONGLONG>(sizeBytes);

            if(SetFilePointerEx(nativeHandle(state), target, nullptr, FILE_BEGIN) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::ResizeFailed);
            }

            if(SetEndOfFile(nativeHandle(state)) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::ResizeFailed);
            }

            if(restorePosition && !state.appendMode && originalPosition.position <= sizeBytes)
            {
                return seekNativeHandle(nativeHandle(state), static_cast<std::int64_t>(originalPosition.position), IO::Types::SeekOrigin::Begin);
            }

            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    NativeLockResult tryLockFile(Internal::FileState &state, Types::Lock::Mode mode) noexcept
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
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
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
                return {.status = makeLastErrorStatus(ErrorCode::LockFailed)};
            }

            UniqueHandle duplicatedHandle{duplicatedHandleRaw};

            OVERLAPPED overlapped{};

            if(LockFileEx(duplicatedHandle.get(), flags, 0, MAXDWORD, MAXDWORD, &overlapped) == FALSE)
            {
                const DWORD error = GetLastError();

                if(error == ERROR_LOCK_VIOLATION || error == ERROR_SHARING_VIOLATION)
                {
                    return {.status = IO::successStatus(), .outcome = Types::Lock::Outcome::WouldBlock};
                }

                return {.status = makeWin32Status(error, ErrorCode::LockFailed)};
            }

            lockState->nativeHandle = duplicatedHandle.release();
            lockState->activeLocks = state.activeLocks;
            lockState->active = true;
            lockState->exclusive = mode == Types::Lock::Mode::Exclusive;
            ++(*lockState->activeLocks);

            return {.status = IO::successStatus(), .outcome = Types::Lock::Outcome::Acquired, .state = std::move(lockState)};
        }
        catch(const std::bad_alloc &)
        {
            return {.status = IO::makeStatus(ErrorCode::OutOfMemory)};
        }
        catch(...)
        {
            return {.status = IO::makeStatus(ErrorCode::Unknown)};
        }
    }

    IO::Types::Status unlockFile(Internal::FileLockState &state) noexcept
    {
        return unlockNativeFile(state);
    }

} // namespace GameWIP::FileSystem::Platform
