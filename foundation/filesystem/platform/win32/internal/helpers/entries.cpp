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
    [[nodiscard]] Result<HandleInfo> openFullPathFollowAll(const std::wstring &path)
    {
        UniqueHandle handle{CreateFileW(path.c_str(), kQueryAccess, kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};

        if(!handle.isValid())
        {
            return failure<HandleInfo>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        return Result<HandleInfo>::success(HandleInfo{.handle = std::move(handle)});
    }

    /// @brief Converts Windows 100-nanosecond ticks to system_clock with overflow checks.
    [[nodiscard]] Result<Types::FileTime> fileTimeToSystemTimePoint(LARGE_INTEGER fileTime)
    {
        constexpr std::int64_t nanosecondsPerTick = 100;
        constexpr std::int64_t minimumTickDelta = std::numeric_limits<std::int64_t>::min() / nanosecondsPerTick;
        constexpr std::int64_t maximumTickDelta = std::numeric_limits<std::int64_t>::max() / nanosecondsPerTick;
        constexpr std::int64_t minimumConvertibleFileTime = kUnixEpochAsWindowsFileTime + minimumTickDelta;
        constexpr std::int64_t maximumConvertibleFileTime = kUnixEpochAsWindowsFileTime + maximumTickDelta;

        if(fileTime.QuadPart < minimumConvertibleFileTime || fileTime.QuadPart > maximumConvertibleFileTime)
        {
            return failure<Types::FileTime>(ErrorCode::SizeLimitExceeded);
        }

        const std::int64_t ticksSinceUnixEpoch = fileTime.QuadPart - kUnixEpochAsWindowsFileTime;
        const std::int64_t nanoseconds = ticksSinceUnixEpoch * nanosecondsPerTick;
        const auto duration = std::chrono::duration_cast<Types::FileTime::duration>(std::chrono::nanoseconds{nanoseconds});
        return Result<Types::FileTime>::success(Types::FileTime{duration});
    }

    /// @brief Detects name-surrogate reparse points, including symlinks and mount points.
    [[nodiscard]] bool isNameSurrogateReparsePoint(const FILE_ATTRIBUTE_TAG_INFO &attributes) noexcept
    {
        return (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && IsReparseTagNameSurrogate(attributes.ReparseTag);
    }

    /// @brief Collects portable entry metadata from one already-stable native handle.
    [[nodiscard]] Result<Types::EntryInfo> queryHandleInfo(HANDLE handle)
    {
        FILE_ATTRIBUTE_TAG_INFO attributeInfo{};

        if(!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributeInfo, sizeof(attributeInfo)))
        {
            return failure<Types::EntryInfo>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        FILE_STANDARD_INFO standardInfo{};

        if(!GetFileInformationByHandleEx(handle, FileStandardInfo, &standardInfo, sizeof(standardInfo)))
        {
            return failure<Types::EntryInfo>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        FILE_BASIC_INFO basicInfo{};

        if(!GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)))
        {
            return failure<Types::EntryInfo>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        Types::EntryInfo info{};

        if(isNameSurrogateReparsePoint(attributeInfo))
        {
            info.kind = Types::EntryKind::Symlink;
        }
        else if(standardInfo.Directory != FALSE || (attributeInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            info.kind = Types::EntryKind::Directory;
        }
        else if((attributeInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            info.kind = Types::EntryKind::Other;
        }
        else
        {
            info.kind = Types::EntryKind::RegularFile;
        }

        if(info.kind == Types::EntryKind::RegularFile)
        {
            if(standardInfo.EndOfFile.QuadPart < 0)
            {
                return failure<Types::EntryInfo>(ErrorCode::SizeLimitExceeded);
            }

            info.sizeBytes = static_cast<std::uint64_t>(standardInfo.EndOfFile.QuadPart);
            info.hasSize = true;
        }

        auto lastWriteTime = fileTimeToSystemTimePoint(basicInfo.LastWriteTime);

        if(!lastWriteTime.ok())
        {
            return failure<Types::EntryInfo>(lastWriteTime.status());
        }

        info.lastWriteTime = std::move(lastWriteTime).value();
        info.hasLastWriteTime = true;
        info.readOnly = (basicInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        return Result<Types::EntryInfo>::success(std::move(info));
    }

    /// @brief Traverses parsed components by handle and applies final/intermediate symlink policy.
    [[nodiscard]] Result<Types::EntryInfo> queryStrictPath(const ParsedPath &path, Types::SymlinkPolicy symlinkPolicy)
    {
        const bool doNotFollowRoot = !path.components.empty() || symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
        auto parent = openRootDirectory(path.root, kShareAll, kDirectoryTraversalAccess, doNotFollowRoot);

        if(!parent.ok())
        {
            return failure<Types::EntryInfo>(parent.status());
        }

        // Return root metadata when no components remain.
        if(path.components.empty())
        {
            return queryHandleInfo(parent.value().handle.get());
        }

        // Resolve parent components without following reparse points.
        for(std::size_t index = 0; index + 1 < path.components.size(); ++index)
        {
            auto child = openChild(parent.value().handle.get(), path.components[index], true, true);

            if(!child.ok())
            {
                return failure<Types::EntryInfo>(child.status());
            }

            auto childInfo = queryHandleInfo(child.value().handle.get());

            if(!childInfo.ok())
            {
                return failure<Types::EntryInfo>(childInfo.status());
            }

            if(childInfo.value().kind == Types::EntryKind::Symlink)
            {
                return failure<Types::EntryInfo>(symlinkPolicyRejectedStatus());
            }

            if(childInfo.value().kind != Types::EntryKind::Directory)
            {
                return failure<Types::EntryInfo>(ErrorCode::NotDirectory);
            }

            parent.value().handle = std::move(child).value().handle;
        }

        // Apply the selected symlink policy to the final component.
        const bool doNotFollowFinal = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
        auto finalHandle = openChild(parent.value().handle.get(), path.components.back(), doNotFollowFinal, false);

        if(!finalHandle.ok())
        {
            return failure<Types::EntryInfo>(finalHandle.status());
        }

        return queryHandleInfo(finalHandle.value().handle.get());
    }

    /// @brief Selects ordinary or strict metadata resolution for one public path query.
    [[nodiscard]] Result<Types::EntryInfo> queryEntryImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
    {
        switch(symlinkPolicy)
        {
        case Types::SymlinkPolicy::DoNotFollow:
        case Types::SymlinkPolicy::FollowFinal:
        case Types::SymlinkPolicy::FollowAll:
            break;
        default:
            return failure<Types::EntryInfo>(ErrorCode::InvalidArgument);
        }

        auto absolutePath = absoluteNativePath(path);

        if(!absolutePath.ok())
        {
            return failure<Types::EntryInfo>(absolutePath.status());
        }

        if(symlinkPolicy == Types::SymlinkPolicy::FollowAll)
        {
            auto handle = openFullPathFollowAll(absolutePath.value());

            if(!handle.ok())
            {
                return failure<Types::EntryInfo>(handle.status());
            }

            return queryHandleInfo(handle.value().handle.get());
        }

        auto parsedPath = parseAbsolutePath(absolutePath.value());

        if(!parsedPath.ok())
        {
            return failure<Types::EntryInfo>(parsedPath.status());
        }

        return queryStrictPath(parsedPath.value(), symlinkPolicy);
    }

    /// ------------------------------------------------------------
    /// Handle metadata and deletion
    /// ------------------------------------------------------------

    /// @brief Marks an open stable handle for POSIX-style deletion when supported.
    [[nodiscard]] Result<void> markHandleForDeletion(HANDLE handle, ErrorCode fallback) noexcept
    {
        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;

        if(SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(fallback));
        }

        return success();
    }

    /// @brief Updates the read-only attribute through an already-stable handle.
    [[nodiscard]] Result<void> setHandleReadOnly(HANDLE handle, bool readOnly) noexcept
    {
        FILE_BASIC_INFO basicInfo{};

        if(GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::StatFailed));
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
            return failure<void>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        return success();
    }

    /// @brief Copies portable timestamps and read-only state between stable handles.
    [[nodiscard]] Result<void> copyHandleBasicMetadata(HANDLE source, HANDLE destination) noexcept
    {
        FILE_BASIC_INFO sourceInfo{};

        if(GetFileInformationByHandleEx(source, FileBasicInfo, &sourceInfo, sizeof(sourceInfo)) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        FILE_BASIC_INFO destinationInfo{};

        if(GetFileInformationByHandleEx(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
        {
            return failure<void>(makeLastErrorStatus(ErrorCode::StatFailed));
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
            return failure<void>(makeLastErrorStatus(ErrorCode::StatFailed));
        }

        return success();
    }

} // namespace GameWIP::FileSystem::Platform::Win32
