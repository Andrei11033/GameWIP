/// @file resolution.cpp
/// @brief Win32 FileSystem stable path resolution.

#include "filesystem/platform/win32/internal/helpers.h"

#include <utility>

namespace GameWIP::FileSystem::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Stable path resolution
    /// ------------------------------------------------------------

    /// @brief Opens a drive or UNC root with the requested access and reparse behavior.
    [[nodiscard]] HandleResult openRootDirectory(
        const std::wstring &root,
        DWORD shareMode,
        ACCESS_MASK desiredAccess,
        bool doNotFollow,
        ErrorCode fallback)
    {
        DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;

        if(doNotFollow)
        {
            flags |= FILE_FLAG_OPEN_REPARSE_POINT;
        }

        UniqueHandle handle{CreateFileW(root.c_str(), desiredAccess, shareMode, nullptr, OPEN_EXISTING, flags, nullptr)};

        if(!handle.isValid())
        {
            return {.status = makeLastErrorStatus(fallback)};
        }

        return {.status = IO::successStatus(), .handle = std::move(handle)};
    }

    /// @brief Calls NtCreateFile relative to a parent handle with explicit reparse behavior.
    [[nodiscard]] HandleResult openChildNative(
        HANDLE parent,
        const std::wstring &name,
        ACCESS_MASK desiredAccess,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions,
        ULONG fileAttributes,
        ErrorCode fallback)
    {
        const NtApi &api = ntApi();

        if(api.createFile == nullptr)
        {
            return {.status = IO::makeStatus(ErrorCode::Unsupported, 0, "NtCreateFile is unavailable")};
        }

        UNICODE_STRING objectName{};
        objectName.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
        objectName.MaximumLength = objectName.Length;
        objectName.Buffer = const_cast<PWSTR>(name.data());

        OBJECT_ATTRIBUTES attributes{};
        InitializeObjectAttributes(&attributes, &objectName, OBJ_CASE_INSENSITIVE, parent, nullptr);

        IO_STATUS_BLOCK ioStatus{};
        HANDLE rawHandle = INVALID_HANDLE_VALUE;

        const NTSTATUS status = api.createFile(
            &rawHandle,
            desiredAccess | SYNCHRONIZE,
            &attributes,
            &ioStatus,
            nullptr,
            fileAttributes,
            shareAccess,
            createDisposition,
            createOptions,
            nullptr,
            0);

        if(!NT_SUCCESS(status))
        {
            return {.status = makeNtStatus(status, fallback)};
        }

        return {.status = IO::successStatus(), .handle = UniqueHandle{rawHandle}, .information = ioStatus.Information};
    }

    /// @brief Opens one child and rejects reparse points when the policy requires it.
    [[nodiscard]] HandleResult openChild(HANDLE parent, const std::wstring &name, bool doNotFollow, bool forTraversal, DWORD shareMode)
    {
        ULONG openOptions = kOpenOptionsBase;

        if(forTraversal)
        {
            openOptions |= FILE_DIRECTORY_FILE;
        }

        if(doNotFollow)
        {
            openOptions |= FILE_OPEN_REPARSE_POINT;
        }

        return openChildNative(
            parent,
            name,
            forTraversal ? kDirectoryTraversalAccess : kQueryAccess,
            shareMode,
            FILE_OPEN,
            openOptions,
            FILE_ATTRIBUTE_NORMAL,
            ErrorCode::StatFailed);
    }

    /// @brief Traverses and retains the parent directory required for a race-resistant final open.
    [[nodiscard]] HandleResult openParentStrict(const ParsedPathResult &path, DWORD shareMode, std::vector<UniqueHandle> *heldHandles)
    {
        if(path.components.empty())
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        // Open the root and retain it when the caller needs the full traversal chain.
        HandleResult parent = openRootDirectory(path.root, shareMode);

        if(!parent.status.ok())
        {
            return parent;
        }

        if(heldHandles != nullptr)
        {
            heldHandles->push_back(UniqueHandle{});
            heldHandles->back() = std::move(parent.handle);
            parent.handle = UniqueHandle{};
        }

        HANDLE currentParent = heldHandles != nullptr ? heldHandles->back().get() : parent.handle.get();

        // Resolve each parent directory without following reparse points.
        for(std::size_t index = 0; index + 1 < path.components.size(); ++index)
        {
            HandleResult child = openChild(currentParent, path.components[index], true, true, shareMode);

            if(!child.status.ok())
            {
                return child;
            }
            EntryQueryResult childInfo = queryHandleInfo(child.handle.get());

            if(!childInfo.status.ok())
            {
                return {.status = std::move(childInfo.status)};
            }

            if(childInfo.info.kind == Types::EntryKind::Symlink)
            {
                return {.status = symlinkPolicyRejectedStatus()};
            }

            if(childInfo.info.kind != Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
            }

            if(heldHandles != nullptr)
            {
                heldHandles->push_back(std::move(child.handle));
                currentParent = heldHandles->back().get();
            }
            else
            {
                parent.handle = std::move(child.handle);
                currentParent = parent.handle.get();
            }
        }

        // Return either the retained ancestry or the final parent handle.
        if(heldHandles != nullptr)
        {
            return {.status = IO::successStatus(), .handle = UniqueHandle{}};
        }

        return parent;
    }

    /// @brief Opens an existing final entry with requested access and symlink behavior.
    [[nodiscard]] OpenPathResult openExistingPath(
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy,
        ACCESS_MASK desiredAccess,
        DWORD shareMode,
        bool allowDirectory,
        ErrorCode fallback)
    {
        WidePathResult absolutePath = absoluteNativePath(path);

        if(!absolutePath.status.ok())
        {
            return {.status = std::move(absolutePath.status)};
        }

        // Use ordinary Win32 resolution when the policy follows every symlink.
        if(symlinkPolicy == Types::SymlinkPolicy::FollowAll)
        {
            DWORD flags = FILE_ATTRIBUTE_NORMAL;

            if(allowDirectory)
            {
                flags |= FILE_FLAG_BACKUP_SEMANTICS;
            }

            UniqueHandle handle{CreateFileW(absolutePath.path.c_str(), desiredAccess, shareMode, nullptr, OPEN_EXISTING, flags, nullptr)};

            if(!handle.isValid())
            {
                return {.status = makeLastErrorStatus(fallback)};
            }
            EntryQueryResult info = queryHandleInfo(handle.get());

            if(!info.status.ok())
            {
                return {.status = std::move(info.status)};
            }

            if(!allowDirectory && info.info.kind == Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::IsDirectory)};
            }

            return {.status = IO::successStatus(), .handle = std::move(handle), .info = info.info};
        }

        ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);

        if(!parsedPath.status.ok())
        {
            return {.status = std::move(parsedPath.status)};
        }

        // Open roots directly because they have no parent components.
        if(parsedPath.components.empty())
        {
            HandleResult root =
                openRootDirectory(parsedPath.root, shareMode, desiredAccess, symlinkPolicy == Types::SymlinkPolicy::DoNotFollow, fallback);

            if(!root.status.ok())
            {
                return {.status = std::move(root.status)};
            }
            EntryQueryResult info = queryHandleInfo(root.handle.get());

            if(!info.status.ok())
            {
                return {.status = std::move(info.status)};
            }

            if(!allowDirectory && info.info.kind == Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::IsDirectory)};
            }

            return {.status = IO::successStatus(), .handle = std::move(root.handle), .info = info.info};
        }

        // Resolve the parent with stable handles for strict symlink policies.
        HandleResult parent = openParentStrict(parsedPath, shareMode);

        if(!parent.status.ok())
        {
            return {.status = std::move(parent.status)};
        }

        ULONG openOptions = kOpenOptionsBase;

        if(!allowDirectory)
        {
            openOptions |= FILE_NON_DIRECTORY_FILE;
        }

        if(symlinkPolicy == Types::SymlinkPolicy::DoNotFollow)
        {
            openOptions |= FILE_OPEN_REPARSE_POINT;
        }

        HandleResult finalHandle = openChildNative(
            parent.handle.get(),
            parsedPath.components.back(),
            desiredAccess,
            shareMode,
            FILE_OPEN,
            openOptions,
            FILE_ATTRIBUTE_NORMAL,
            fallback);

        if(!finalHandle.status.ok())
        {
            return {.status = std::move(finalHandle.status)};
        }
        EntryQueryResult info = queryHandleInfo(finalHandle.handle.get());

        if(!info.status.ok())
        {
            return {.status = std::move(info.status)};
        }

        if(!allowDirectory && info.info.kind == Types::EntryKind::Directory)
        {
            return {.status = IO::makeStatus(ErrorCode::IsDirectory)};
        }

        return {.status = IO::successStatus(), .handle = std::move(finalHandle.handle), .info = info.info};
    }

    /// @brief Retains every traversal handle so a multi-step operation cannot swap path components.
    [[nodiscard]] StablePathResult stabilizeExistingPath(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy, bool requireDirectory)
    {
        WidePathResult absolutePath = absoluteNativePath(path);

        if(!absolutePath.status.ok())
        {
            return {.status = std::move(absolutePath.status)};
        }

        // Keep one stable handle when ordinary symlink traversal is allowed.
        if(symlinkPolicy == Types::SymlinkPolicy::FollowAll)
        {
            OpenPathResult opened = openExistingPath(path, symlinkPolicy, kQueryAccess, kShareWithoutDelete, true, ErrorCode::StatFailed);

            if(!opened.status.ok())
            {
                return {.status = std::move(opened.status)};
            }

            if(requireDirectory && opened.info.kind != Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
            }
            std::vector<UniqueHandle> handles;
            handles.push_back(std::move(opened.handle));
            return {.status = IO::successStatus(), .handles = std::move(handles), .info = opened.info};
        }

        ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);

        if(!parsedPath.status.ok())
        {
            return {.status = std::move(parsedPath.status)};
        }

        std::vector<UniqueHandle> handles;
        const DWORD stableShare = kShareWithoutDelete;

        // Keep the root handle as the complete stable path.
        if(parsedPath.components.empty())
        {
            const bool doNotFollowRoot = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
            HandleResult root = openRootDirectory(parsedPath.root, stableShare, kDirectoryTraversalAccess, doNotFollowRoot);

            if(!root.status.ok())
            {
                return {.status = std::move(root.status)};
            }
            EntryQueryResult info = queryHandleInfo(root.handle.get());

            if(!info.status.ok())
            {
                return {.status = std::move(info.status)};
            }

            if(requireDirectory && info.info.kind != Types::EntryKind::Directory)
            {
                return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
            }
            handles.push_back(std::move(root.handle));
            return {.status = IO::successStatus(), .handles = std::move(handles), .info = info.info};
        }

        // Retain each resolved component so later operations use the same ancestry.
        HandleResult parent = openParentStrict(parsedPath, stableShare, &handles);

        if(!parent.status.ok())
        {
            return {.status = std::move(parent.status)};
        }

        const bool doNotFollowFinal = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
        HandleResult finalHandle = openChild(handles.back().get(), parsedPath.components.back(), doNotFollowFinal, requireDirectory, stableShare);

        if(!finalHandle.status.ok())
        {
            return {.status = std::move(finalHandle.status)};
        }
        EntryQueryResult info = queryHandleInfo(finalHandle.handle.get());

        if(!info.status.ok())
        {
            return {.status = std::move(info.status)};
        }

        if(requireDirectory && info.info.kind != Types::EntryKind::Directory)
        {
            return {.status = IO::makeStatus(ErrorCode::NotDirectory)};
        }

        handles.push_back(std::move(finalHandle.handle));
        return {.status = IO::successStatus(), .handles = std::move(handles), .info = info.info};
    }

} // namespace GameWIP::FileSystem::Platform::Win32
