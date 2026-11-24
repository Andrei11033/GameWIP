/// @file entries.cpp
/// @brief Win32 FileSystem entry queries, metadata, and deletion helpers.

#include "filesystem/platform/win32/internal/helpers.h"

#include <chrono>
#include <limits>
#include <utility>

namespace GameWIP::FileSystem::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Entry queries and metadata conversion
    /// ------------------------------------------------------------

    /// @brief Opens a complete path using ordinary Win32 symlink-following semantics.
    [[nodiscard]] HandleResult openFullPathFollowAll(const std::wstring &path)
    {
        UniqueHandle handle{CreateFileW(path.c_str(), kQueryAccess, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};

        if(!handle.isValid())
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        return {.status = IO::successStatus(), .handle = std::move(handle)};
    }

    /// @brief Converts Windows 100-nanosecond ticks to system_clock with overflow checks.
    [[nodiscard]] TimeResult fileTimeToSystemTimePoint(LARGE_INTEGER fileTime)
    {
        constexpr std::int64_t nanosecondsPerTick = 100;
        constexpr std::int64_t minimumTickDelta = std::numeric_limits<std::int64_t>::min() / nanosecondsPerTick;
        constexpr std::int64_t maximumTickDelta = std::numeric_limits<std::int64_t>::max() / nanosecondsPerTick;
        constexpr std::int64_t minimumConvertibleFileTime = kUnixEpochAsWindowsFileTime + minimumTickDelta;
        constexpr std::int64_t maximumConvertibleFileTime = kUnixEpochAsWindowsFileTime + maximumTickDelta;

        if(fileTime.QuadPart < minimumConvertibleFileTime || fileTime.QuadPart > maximumConvertibleFileTime)
        {
            return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
        }

        const std::int64_t ticksSinceUnixEpoch = fileTime.QuadPart - kUnixEpochAsWindowsFileTime;
        const std::int64_t nanoseconds = ticksSinceUnixEpoch * nanosecondsPerTick;
        const auto duration = std::chrono::duration_cast<Types::FileTime::duration>(std::chrono::nanoseconds{nanoseconds});
        return {.status = IO::successStatus(), .time = Types::FileTime{duration}};
    }

    /// @brief Detects name-surrogate reparse points, including symlinks and mount points.
    [[nodiscard]] bool isNameSurrogateReparsePoint(const FILE_ATTRIBUTE_TAG_INFO &attributes) noexcept
    {
        return (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(attributes.ReparseTag);
    }

    /// @brief Collects portable entry metadata from one already-stable native handle.
    [[nodiscard]] EntryQueryResult queryHandleInfo(HANDLE handle)
    {
        FILE_ATTRIBUTE_TAG_INFO attributeInfo{};

        if(!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributeInfo, sizeof(attributeInfo)))
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        FILE_STANDARD_INFO standardInfo{};

        if(!GetFileInformationByHandleEx(handle, FileStandardInfo, &standardInfo, sizeof(standardInfo)))
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        FILE_BASIC_INFO basicInfo{};

        if(!GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
        {
            return {.status = makeLastErrorStatus(ErrorCode::StatFailed)};
        }

        EntryQueryResult result{.status = IO::successStatus()};

        if(isNameSurrogateReparsePoint(attributeInfo))
        {
            result.info.kind = Types::EntryKind::Symlink;
        }
        else if(standardInfo.Directory != FALSE || (attributeInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            result.info.kind = Types::EntryKind::Directory;
        }
        else if((attributeInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            result.info.kind = Types::EntryKind::Other;
        }
        else
        {
            result.info.kind = Types::EntryKind::RegularFile;
        }

        if(result.info.kind == Types::EntryKind::RegularFile)
        {
            if(standardInfo.EndOfFile.QuadPart < 0)
            {
                return {.status = IO::makeStatus(ErrorCode::SizeLimitExceeded)};
            }

            result.info.sizeBytes = static_cast<std::uint64_t>(standardInfo.EndOfFile.QuadPart);
            result.info.hasSize = true;
        }

        TimeResult lastWriteTime = fileTimeToSystemTimePoint(basicInfo.LastWriteTime);

        if(!lastWriteTime.status.ok())
        {
            return {.status = std::move(lastWriteTime.status)};
        }

        result.info.lastWriteTime = lastWriteTime.time;
        result.info.hasLastWriteTime = true;
        result.info.readOnly = (basicInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        return result;
    }

    /// @brief Traverses parsed components by handle and applies final/intermediate symlink policy.
    [[nodiscard]] EntryQueryResult queryStrictPath(const ParsedPathResult &path, Types::SymlinkPolicy symlinkPolicy)
    {
        const bool doNotFollowRoot = !path.components.empty() || symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
        HandleResult parent = openRootDirectory(path.root, kShareAll, kDirectoryTraversalAccess, doNotFollowRoot);

        if(!parent.status.ok())
        {
            return {.status = std::move(parent.status)};
        }

        // Return root metadata when no components remain.
        if(path.components.empty())
        {
            return queryHandleInfo(parent.handle.get());
        }

        // Resolve parent components without following reparse points.
        for(std::size_t index = 0; index + 1 < path.components.size(); ++index)
        {
            HandleResult child = openChild(parent.handle.get(), path.components[index], true, true);

            if(!child.status.ok())
            {
                return {.status = std::move(child.status)};
            }

            EntryQueryResult childInfo = queryHandleInfo(child.handle.get());

            if(!childInfo.status.ok())
            {
                return childInfo;
            }

            if(childInfo.info.kind == Types::EntryKind::Symlink)
            {
                return {.status = symlinkPolicyRejectedStatus()};
            }

            if(childInfo.info.kind != Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
            }

            parent.handle = std::move(child.handle);
        }

        // Apply the selected symlink policy to the final component.
        const bool doNotFollowFinal = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
        HandleResult finalHandle = openChild(parent.handle.get(), path.components.back(), doNotFollowFinal, false);

        if(!finalHandle.status.ok())
        {
            return {.status = std::move(finalHandle.status)};
        }

        return queryHandleInfo(finalHandle.handle.get());
    }

    /// @brief Selects ordinary or strict metadata resolution for one public path query.
    [[nodiscard]] EntryQueryResult queryEntryImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
    {
        switch(symlinkPolicy)
        {
        case Types::SymlinkPolicy::DoNotFollow:
        case Types::SymlinkPolicy::FollowFinal:
        case Types::SymlinkPolicy::FollowAll:
            break;
        default:
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        WidePathResult absolutePath = absoluteNativePath(path);

        if(!absolutePath.status.ok())
        {
            return {.status = std::move(absolutePath.status)};
        }

        if(symlinkPolicy == Types::SymlinkPolicy::FollowAll)
        {
            HandleResult handle = openFullPathFollowAll(absolutePath.path);

            if(!handle.status.ok())
            {
                return {.status = std::move(handle.status)};
            }

            return queryHandleInfo(handle.handle.get());
        }

        ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);

        if(!parsedPath.status.ok())
        {
            return {.status = std::move(parsedPath.status)};
        }

        return queryStrictPath(parsedPath, symlinkPolicy);
    }

    /// ------------------------------------------------------------
    /// Handle metadata and deletion
    /// ------------------------------------------------------------

    /// @brief Marks an open stable handle for POSIX-style deletion when supported.
    [[nodiscard]] IO::Types::Status markHandleForDeletion(HANDLE handle, ErrorCode fallback) noexcept
    {
        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;

        if(SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) == FALSE)
        {
            return makeLastErrorStatus(fallback);
        }

        return IO::successStatus();
    }

    /// @brief Updates the read-only attribute through an already-stable handle.
    [[nodiscard]] IO::Types::Status setHandleReadOnly(HANDLE handle, bool readOnly) noexcept
    {
        FILE_BASIC_INFO basicInfo{};

        if(GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        if(readOnly)
        {
            basicInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
        }
        else
        {
            basicInfo.FileAttributes &= kAttributesWithoutReadOnly;

            if(basicInfo.FileAttributes == 0)
            {
                basicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
        }

        if(SetFileInformationByHandle(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        return IO::successStatus();
    }

    /// @brief Copies portable timestamps and read-only state between stable handles.
    [[nodiscard]] IO::Types::Status copyHandleBasicMetadata(HANDLE source, HANDLE destination) noexcept
    {
        FILE_BASIC_INFO sourceInfo{};

        if(GetFileInformationByHandleEx(source, FileBasicInfo, &sourceInfo, sizeof(sourceInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        FILE_BASIC_INFO destinationInfo{};

        if(GetFileInformationByHandleEx(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        // Preserve destination attributes while copying portable source fields.
        destinationInfo.LastWriteTime = sourceInfo.LastWriteTime;

        if((sourceInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
        {
            destinationInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
        }
        else
        {
            destinationInfo.FileAttributes &= kAttributesWithoutReadOnly;

            if(destinationInfo.FileAttributes == 0)
            {
                destinationInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
        }

        if(SetFileInformationByHandle(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        return IO::successStatus();
    }

} // namespace GameWIP::FileSystem::Platform::Win32
