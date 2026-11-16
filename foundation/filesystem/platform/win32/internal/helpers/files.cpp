/// @file files.cpp
/// @brief Win32 FileSystem native file operations.

#include "filesystem/platform/win32/internal/helpers.h"

namespace GameWIP::FileSystem::Detail::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Native file-open policy
    /// ------------------------------------------------------------

    /// @brief Enforces an open request's existing-entry kind and replacement rules.
    [[nodiscard]] IO::Types::Status validateExistingEntry(const Types::Path &path, const NativeOpenRequest &request)
    {
        EntryQueryResult entry = queryEntryImpl(path, request.symlinkPolicy);

        if(!entry.status.ok())
        {
            if(entry.status.code == ErrorCode::NotFound && request.existingEntryRule == ExistingEntryRule::MayCreateRegularFile)
            {
                return IO::successStatus();
            }
            return std::move(entry.status);
        }

        if(request.creationDisposition == CREATE_NEW)
        {
            return IO::makeStatus(ErrorCode::AlreadyExists);
        }

        if(entry.info.kind == Types::EntryKind::Directory)
        {
            return IO::makeStatus(ErrorCode::IsDirectory);
        }

        if(entry.info.kind != Types::EntryKind::RegularFile)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        return IO::successStatus();
    }

    /// @brief Publishes a successfully opened native handle into platform-neutral file state.
    [[nodiscard]] IO::Types::Status assignFileState(std::unique_ptr<Detail::FileState> &state, UniqueHandle handle, const NativeOpenRequest &request)
    {
        auto newState = std::make_unique<Detail::FileState>();
        newState->nativeHandle = handle.release();
        newState->access = request.access;
        newState->flushOnClose = request.flushOnClose;
        newState->readable = request.readable;
        newState->writable = request.writable;
        newState->appendMode = request.appendMode;

        state = std::move(newState);
        return IO::successStatus();
    }

    /// @brief Moves a native file pointer using one validated seek origin.
    [[nodiscard]] IO::Types::Status seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin);
    /// @brief Queries the current unsigned native file position.
    [[nodiscard]] IO::Types::PositionResult nativePosition(HANDLE handle);

    /// @brief Resizes a handle and reports a failed restoration of its original file pointer.
    [[nodiscard]] IO::Types::Status truncateNativeHandle(HANDLE handle, std::uint64_t sizeBytes)
    {
        if(sizeBytes > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return IO::makeStatus(ErrorCode::SizeLimitExceeded);
        }

        const IO::Types::PositionResult originalPosition = nativePosition(handle);

        if(!originalPosition.status.ok())
        {
            return originalPosition.status;
        }

        LARGE_INTEGER target{};
        target.QuadPart = static_cast<LONGLONG>(sizeBytes);

        if(SetFilePointerEx(handle, target, nullptr, FILE_BEGIN) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::ResizeFailed);
        }

        if(SetEndOfFile(handle) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::ResizeFailed);
        }

        if(originalPosition.position <= sizeBytes)
        {
            return seekNativeHandle(handle, static_cast<std::int64_t>(originalPosition.position), IO::Types::SeekOrigin::Begin);
        }

        return IO::successStatus();
    }

    /// @brief Performs a handle-relative final open for non-FollowAll policies.
    [[nodiscard]] IO::Types::Status openNativeFileStrict(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const NativeOpenRequest &request)
    {
        WidePathResult absolutePath = absoluteNativePath(path);

        if(!absolutePath.status.ok())
        {
            return std::move(absolutePath.status);
        }

        ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);

        if(!parsedPath.status.ok())
        {
            return std::move(parsedPath.status);
        }

        if(parsedPath.components.empty())
        {
            return IO::makeStatus(ErrorCode::IsDirectory);
        }

        // Resolve the parent without reopening its validated ancestors.
        HandleResult parent = openParentStrict(parsedPath);

        if(!parent.status.ok())
        {
            return std::move(parent.status);
        }

        ULONG openOptions = kOpenOptionsBase | FILE_NON_DIRECTORY_FILE;

        if(request.symlinkPolicy == Types::SymlinkPolicy::DoNotFollow)
        {
            openOptions |= FILE_OPEN_REPARSE_POINT;
        }

        const auto openFinal = [&](ULONG createDisposition) -> HandleResult
        {
            return openChildNative(
                parent.handle.get(),
                parsedPath.components.back(),
                request.desiredAccess,
                nativeShareMode(request.share),
                createDisposition,
                openOptions,
                FILE_ATTRIBUTE_NORMAL,
                ErrorCode::OpenFailed);
        };

        // Select native creation semantics and any required post-open truncation.
        bool truncateAfterOpen = false;
        HandleResult finalHandle{.status = IO::makeStatus(ErrorCode::InvalidArgument)};

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

            if(!finalHandle.status.ok() && finalHandle.status.code == ErrorCode::AlreadyExists)
            {
                finalHandle = openFinal(FILE_OPEN);
                truncateAfterOpen = true;
            }
            break;
        default:
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        if(!finalHandle.status.ok())
        {
            return std::move(finalHandle.status);
        }

        // Validate the opened entry before truncating or publishing its handle.
        EntryQueryResult info = queryHandleInfo(finalHandle.handle.get());

        if(!info.status.ok())
        {
            return std::move(info.status);
        }

        if(info.info.kind == Types::EntryKind::Directory)
        {
            return IO::makeStatus(ErrorCode::IsDirectory);
        }

        if(info.info.kind != Types::EntryKind::RegularFile)
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        if(truncateAfterOpen)
        {
            IO::Types::Status truncateStatus = truncateNativeHandle(finalHandle.handle.get(), 0);

            if(!truncateStatus.ok())
            {
                return truncateStatus;
            }
        }

        return assignFileState(state, std::move(finalHandle.handle), request);
    }

    /// @brief Executes one validated native open and initializes FileState on success.
    [[nodiscard]] IO::Types::Status openNativeFile(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const NativeOpenRequest &request)
    {
        if(path.empty() || !isValidFileShare(request.share) || !isValidSymlinkPolicy(request.symlinkPolicy) ||
           !IO::isValidFlushMode(request.flushOnClose))
        {
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        // Use handle-relative traversal when the policy restricts symlink following.
        if(request.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
        {
            return openNativeFileStrict(state, path, request);
        }

        // Check existing-entry and replacement policy before opening the full path.
        IO::Types::Status existingStatus = validateExistingEntry(path, request);

        if(!existingStatus.ok())
        {
            return existingStatus;
        }

        WidePathResult absolutePath = absoluteNativePath(path);

        if(!absolutePath.status.ok())
        {
            return std::move(absolutePath.status);
        }

        UniqueHandle handle{CreateFileW(
            absolutePath.path.c_str(),
            request.desiredAccess,
            nativeShareMode(request.share),
            nullptr,
            request.creationDisposition,
            request.flagsAndAttributes,
            nullptr)};

        if(!handle.isValid())
        {
            return makeLastErrorStatus(ErrorCode::OpenFailed);
        }
        return assignFileState(state, std::move(handle), request);
    }

    /// ------------------------------------------------------------
    /// Open-file operations and metadata
    /// ------------------------------------------------------------

    /// @brief Implements native seek after validating origin and signed offset semantics.
    [[nodiscard]] IO::Types::Status seekNativeHandle(HANDLE handle, std::int64_t offset, IO::Types::SeekOrigin origin)
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
            return IO::makeStatus(ErrorCode::InvalidArgument);
        }

        LARGE_INTEGER distance{};
        distance.QuadPart = offset;

        if(SetFilePointerEx(handle, distance, nullptr, moveMethod) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::SeekFailed);
        }

        return IO::successStatus();
    }

    /// @brief Returns the current native file pointer without changing it.
    [[nodiscard]] IO::Types::PositionResult nativePosition(HANDLE handle)
    {
        LARGE_INTEGER distance{};
        LARGE_INTEGER position{};

        if(SetFilePointerEx(handle, distance, &position, FILE_CURRENT) == FALSE)
        {
            return {.status = makeLastErrorStatus(ErrorCode::SeekFailed)};
        }

        if(position.QuadPart < 0)
        {
            return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
        }

        return {.status = IO::successStatus(), .position = static_cast<std::uint64_t>(position.QuadPart)};
    }

    /// @brief Returns the unsigned file size for one open native handle.
    [[nodiscard]] IO::Types::SizeResult nativeFileSize(HANDLE handle)
    {
        LARGE_INTEGER size{};

        if(GetFileSizeEx(handle, &size) == FALSE)
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        if(size.QuadPart < 0)
        {
            return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
        }

        return {.status = IO::successStatus(), .sizeBytes = static_cast<std::uint64_t>(size.QuadPart)};
    }

    /// @brief Releases a whole-file native lock and decrements shared active-lock state.
    [[nodiscard]] IO::Types::Status unlockNativeFile(Detail::FileLockState &state) noexcept
    {
        if(!state.active)
        {
            return IO::successStatus();
        }

#if FILESYSTEM_INTERNAL_TEST_HOOKS
        if(fileUnlockFailureForced())
        {
            return IO::makeStatus(ErrorCode::UnlockFailed);
        }

#endif

        OVERLAPPED overlapped{};

        if(UnlockFileEx(nativeHandle(state), 0, MAXDWORD, MAXDWORD, &overlapped) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::UnlockFailed);
        }
        state.active = false;

        if(state.activeLocks && *state.activeLocks > 0)
        {
            --(*state.activeLocks);
        }

        return IO::successStatus();
    }

} // namespace GameWIP::FileSystem::Detail::Platform::Win32
