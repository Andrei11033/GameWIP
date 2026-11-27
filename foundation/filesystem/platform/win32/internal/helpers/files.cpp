/// @file files.cpp
/// @brief Win32 FileSystem native file operations.

#include "filesystem/platform/win32/internal/helpers.h"

namespace GameWIP::FileSystem::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Native file-open policy
    /// ------------------------------------------------------------

    /// @brief Enforces an open request's existing-entry kind and replacement rules.
    [[nodiscard]] Result<void> validateExistingEntry(const Types::Path &path, const NativeOpenRequest &request)
    {
        auto entry = queryEntryImpl(path, request.symlinkPolicy);

        if(!entry.ok())
        {
            if(entry.status().code() == ErrorCode::NotFound && request.existingEntryRule == ExistingEntryRule::MayCreateRegularFile)
            {
                return success();
            }
            return failure<void>(entry.status());
        }

        if(request.creationDisposition == CREATE_NEW)
        {
            return failure<void>(ErrorCode::AlreadyExists);
        }

        if(entry.value().kind == Types::EntryKind::Directory)
        {
            return failure<void>(ErrorCode::IsDirectory);
        }

        if(entry.value().kind != Types::EntryKind::RegularFile)
        {
            return failure<void>(ErrorCode::InvalidArgument);
        }

        return success();
    }

    /// @brief Publishes a successfully opened native handle into platform-neutral file state.
    [[nodiscard]] Result<void> assignFileState(std::unique_ptr<Internal::FileState> &state, UniqueHandle handle, const NativeOpenRequest &request)
    {
        auto newState = std::make_unique<Internal::FileState>();
        newState->nativeHandle = handle.release();
        newState->access = request.access;
        newState->flushOnClose = request.flushOnClose;
        newState->readable = request.readable;
        newState->writable = request.writable;
        newState->appendMode = request.appendMode;

        state = std::move(newState);
        return success();
    }

    /// @brief Moves a native file pointer using one validated seek origin.
    [[nodiscard]] Result<void> seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin);
    /// @brief Queries the current unsigned native file position.
    [[nodiscard]] Result<std::uint64_t> nativePosition(HANDLE handle);

    /// @brief Resizes a handle and reports a failed restoration of its original file pointer.
    [[nodiscard]] Result<void> truncateNativeHandle(HANDLE handle, std::uint64_t sizeBytes)
    {
        if(sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return failure<void>(ErrorCode::SizeLimitExceeded);
        }

        auto originalPosition = nativePosition(handle);

        if(!originalPosition.ok())
        {
            return failure<void>(originalPosition.status());
        }

        LARGE_INTEGER target{};
        target.QuadPart = static_cast<LONGLONG>(sizeBytes);

        if(SetFilePointerEx(handle, target, nullptr, FILE_BEGIN) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::ResizeFailed));
        }

        if(SetEndOfFile(handle) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::ResizeFailed));
        }

        if(originalPosition.value() <= sizeBytes)
        {
            return seekNativeHandle(handle, static_cast<std::int64_t>(originalPosition.value()), IO::Types::SeekOrigin::Begin);
        }

        return success();
    }

    /// @brief Performs a handle-relative final open for non-FollowAll policies.
    [[nodiscard]] Result<void> openNativeFileStrict(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const NativeOpenRequest &request)
    {
        auto absolutePath = absoluteNativePath(path);

        if(!absolutePath.ok())
        {
            return failure<void>(absolutePath.status());
        }

        auto parsedPath = parseAbsolutePath(absolutePath.value());

        if(!parsedPath.ok())
        {
            return failure<void>(parsedPath.status());
        }

        if(parsedPath.value().components.empty())
        {
            return failure<void>(ErrorCode::IsDirectory);
        }

        // Resolve the parent without reopening its validated ancestors.
        auto parent = openParentStrict(parsedPath.value());

        if(!parent.ok())
        {
            return failure<void>(parent.status());
        }

        ULONG openOptions = kOpenOptionsBase | FILE_NON_DIRECTORY_FILE;

        if(request.symlinkPolicy == Types::SymlinkPolicy::DoNotFollow)
        {
            openOptions |= FILE_OPEN_REPARSE_POINT;
        }

        const auto openFinal = [&](ULONG createDisposition) -> Result<HandleInfo>
        {
            return openChildNative(
                parent.value().handle.get(),
                parsedPath.value().components.back(),
                request.desiredAccess,
                nativeShareMode(request.share),
                createDisposition,
                openOptions,
                FILE_ATTRIBUTE_NORMAL,
                ErrorCode::OpenFailed);
        };

        // Select native creation semantics and any required post-open truncation.
        bool truncateAfterOpen = false;
        auto finalHandle = failure<HandleInfo>(ErrorCode::InvalidArgument);

        switch(request.creationDisposition)
        {
        case CREATE_NEW:
            finalHandle = openFinal(FILE_CREATE);
            break;
        case OPEN_EXISTING:
            finalHandle = openFinal(FILE_OPEN);
            break;
        case OPEN_ALWAYS:
            finalHandle = openFinal(FILE_OPEN_IF);
            break;
        case TRUNCATE_EXISTING:
            finalHandle = openFinal(FILE_OPEN);
            truncateAfterOpen = true;
            break;
        case CREATE_ALWAYS:
            finalHandle = openFinal(FILE_CREATE);

            if(!finalHandle.ok() && finalHandle.status().code() == ErrorCode::AlreadyExists)
            {
                finalHandle = openFinal(FILE_OPEN);
                truncateAfterOpen = true;
            }
            break;
        default:
            return failure<void>(ErrorCode::InvalidArgument);
        }

        if(!finalHandle.ok())
        {
            return failure<void>(finalHandle.status());
        }

        // Validate the opened entry before truncating or publishing its handle.
        auto info = queryHandleInfo(finalHandle.value().handle.get());

        if(!info.ok())
        {
            return failure<void>(info.status());
        }

        if(info.value().kind == Types::EntryKind::Directory)
        {
            return failure<void>(ErrorCode::IsDirectory);
        }

        if(info.value().kind != Types::EntryKind::RegularFile)
        {
            return failure<void>(ErrorCode::InvalidArgument);
        }

        if(truncateAfterOpen)
        {
            auto truncateResult = truncateNativeHandle(finalHandle.value().handle.get(), 0);

            if(!truncateResult.ok())
            {
                return truncateResult;
            }
        }

        return assignFileState(state, std::move(finalHandle).value().handle, request);
    }

    /// @brief Executes one validated native open and initializes FileState on success.
    [[nodiscard]] Result<void> openNativeFile(std::unique_ptr<Internal::FileState> &state, const Types::Path &path, const NativeOpenRequest &request)
    {
        if(path.empty() || !isValidFileShare(request.share) || !isValidSymlinkPolicy(request.symlinkPolicy) ||
           !IO::isValidFlushMode(request.flushOnClose))
        {
            return failure<void>(ErrorCode::InvalidArgument);
        }

        // Use handle-relative traversal when the policy restricts symlink following.
        if(request.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
        {
            return openNativeFileStrict(state, path, request);
        }

        // Check existing-entry and replacement policy before opening the full path.
        auto existingStatus = validateExistingEntry(path, request);

        if(!existingStatus.ok())
        {
            return existingStatus;
        }

        auto absolutePath = absoluteNativePath(path);

        if(!absolutePath.ok())
        {
            return failure<void>(absolutePath.status());
        }

        UniqueHandle handle{CreateFileW(
            absolutePath.value().c_str(),
            request.desiredAccess,
            nativeShareMode(request.share),
            nullptr,
            request.creationDisposition,
            request.flagsAndAttributes,
            nullptr)};

        if(!handle.isValid())
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::OpenFailed));
        }
        return assignFileState(state, std::move(handle), request);
    }

    /// ------------------------------------------------------------
    /// Open-file operations and metadata
    /// ------------------------------------------------------------

    /// @brief Implements native seek after validating origin and signed offset semantics.
    [[nodiscard]] Result<void> seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin)
    {
        DWORD moveMethod = FILE_BEGIN;

        switch(origin)
        {
        case IO::Types::SeekOrigin::Begin:
            moveMethod = FILE_BEGIN;
            break;
        case IO::Types::SeekOrigin::Current:
            moveMethod = FILE_CURRENT;
            break;
        case IO::Types::SeekOrigin::End:
            moveMethod = FILE_END;
            break;
        default:
            return failure<void>(ErrorCode::InvalidArgument);
        }

        LARGE_INTEGER distance{};
        distance.QuadPart = offset;

        if(SetFilePointerEx(handle, distance, nullptr, moveMethod) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::SeekFailed));
        }

        return success();
    }

    /// @brief Returns the current native file pointer without changing it.
    [[nodiscard]] Result<std::uint64_t> nativePosition(HANDLE handle)
    {
        LARGE_INTEGER distance{};
        LARGE_INTEGER position{};

        if(SetFilePointerEx(handle, distance, &position, FILE_CURRENT) == FALSE)
        {
            return failure<std::uint64_t>(makeLastErrorStatus(ErrorCode::SeekFailed));
        }

        if(position.QuadPart < 0)
        {
            return failure<std::uint64_t>(ErrorCode::SizeLimitExceeded);
        }

        return Result<std::uint64_t>::success(static_cast<std::uint64_t>(position.QuadPart));
    }

    /// @brief Returns the unsigned file size for one open native handle.
    [[nodiscard]] Result<std::uint64_t> nativeFileSize(HANDLE handle)
    {
        LARGE_INTEGER size{};

        if(GetFileSizeEx(handle, &size) == FALSE)
        {
            return failure<std::uint64_t>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        if(size.QuadPart < 0)
        {
            return failure<std::uint64_t>(ErrorCode::SizeLimitExceeded);
        }

        return Result<std::uint64_t>::success(static_cast<std::uint64_t>(size.QuadPart));
    }

    /// @brief Releases a whole-file native lock and decrements shared active-lock state.
    [[nodiscard]] Result<void> unlockNativeFile(Internal::FileLockState &state) noexcept
    {
        if(!state.active)
        {
            return success();
        }

#if FILESYSTEM_INTERNAL_TEST_HOOKS
        if(fileUnlockFailureForced())
        {
            return failure<void>(ErrorCode::UnlockFailed);
        }

#endif

        OVERLAPPED overlapped{};

        if(UnlockFileEx(nativeHandle(state), 0, MAXDWORD, MAXDWORD, &overlapped) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::UnlockFailed));
        }
        state.active = false;

        if(state.activeLocks && *state.activeLocks > 0)
        {
            --(*state.activeLocks);
        }

        return success();
    }

} // namespace GameWIP::FileSystem::Platform::Win32
