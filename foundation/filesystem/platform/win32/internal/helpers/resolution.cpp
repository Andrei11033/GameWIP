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
    [[nodiscard]] Result<HandleInfo> openRootDirectory(
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
            return failure<HandleInfo>(makeLastErrorStatus(fallback));
        }

        return Result<HandleInfo>::success(HandleInfo{.handle = std::move(handle)});
    }

    /// @brief Calls NtCreateFile relative to a parent handle with explicit reparse behavior.
    [[nodiscard]] Result<HandleInfo> openChildNative(
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
            return failure<HandleInfo>(Status::withDiagnostic(ErrorCode::Unsupported, "NtCreateFile is unavailable"));
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
            return failure<HandleInfo>(makeNtStatus(status, fallback));
        }

        return Result<HandleInfo>::success(HandleInfo{.handle = UniqueHandle{rawHandle}, .information = ioStatus.Information});
    }

    /// @brief Opens one child and rejects reparse points when the policy requires it.
    [[nodiscard]] Result<HandleInfo> openChild(HANDLE parent, const std::wstring &name, bool doNotFollow, bool forTraversal, DWORD shareMode)
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
    [[nodiscard]] Result<HandleInfo> openParentStrict(const ParsedPath &path, DWORD shareMode, std::vector<UniqueHandle> *heldHandles)
    {
        if(path.components.empty())
        {
            return failure<HandleInfo>(ErrorCode::InvalidArgument);
        }

        // Open the root and retain it when the caller needs the full traversal chain.
        auto parent = openRootDirectory(path.root, shareMode);

        if(!parent.ok())
        {
            return failure<HandleInfo>(parent.status());
        }

        if(heldHandles != nullptr)
        {
            heldHandles->push_back(std::move(parent).value().handle);
        }

        HANDLE currentParent = heldHandles != nullptr ? heldHandles->back().get() : parent.value().handle.get();

        // Resolve each parent directory without following reparse points.
        for(std::size_t index = 0; index + 1 < path.components.size(); ++index)
        {
            auto child = openChild(currentParent, path.components[index], true, true, shareMode);

            if(!child.ok())
            {
                return failure<HandleInfo>(child.status());
            }

            auto childInfo = queryHandleInfo(child.value().handle.get());

            if(!childInfo.ok())
            {
                return failure<HandleInfo>(childInfo.status());
            }

            if(childInfo.value().kind == Types::EntryKind::Symlink)
            {
                return failure<HandleInfo>(symlinkPolicyRejectedStatus());
            }

            if(childInfo.value().kind != Types::EntryKind::Directory)
            {
                return failure<HandleInfo>(ErrorCode::NotDirectory);
            }

            if(heldHandles != nullptr)
            {
                heldHandles->push_back(std::move(child).value().handle);
                currentParent = heldHandles->back().get();
            }
            else
            {
                parent.value().handle = std::move(child).value().handle;
                currentParent = parent.value().handle.get();
            }
        }

        // Return either the retained ancestry or the final parent handle.
        if(heldHandles != nullptr)
        {
            return Result<HandleInfo>::success(HandleInfo{});
        }

        return parent;
    }

    /// @brief Opens an existing final entry with requested access and symlink behavior.
    [[nodiscard]] Result<OpenPath> openExistingPath(
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy,
        ACCESS_MASK desiredAccess,
        DWORD shareMode,
        bool allowDirectory,
        ErrorCode fallback)
    {
        auto absolutePath = absoluteNativePath(path);

        if(!absolutePath.ok())
        {
            return failure<OpenPath>(absolutePath.status());
        }

        // Use ordinary Win32 resolution when the policy follows every symlink.
        if(symlinkPolicy == Types::SymlinkPolicy::FollowAll)
        {
            DWORD flags = FILE_ATTRIBUTE_NORMAL;

            if(allowDirectory)
            {
                flags |= FILE_FLAG_BACKUP_SEMANTICS;
            }

            UniqueHandle handle{CreateFileW(absolutePath.value().c_str(), desiredAccess, shareMode, nullptr, OPEN_EXISTING, flags, nullptr)};

            if(!handle.isValid())
            {
                return failure<OpenPath>(makeLastErrorStatus(fallback));
            }

            auto info = queryHandleInfo(handle.get());

            if(!info.ok())
            {
                return failure<OpenPath>(info.status());
            }

            if(!allowDirectory && info.value().kind == Types::EntryKind::Directory)
            {
                return failure<OpenPath>(ErrorCode::IsDirectory);
            }

            return Result<OpenPath>::success(OpenPath{.handle = std::move(handle), .info = std::move(info).value()});
        }

        auto parsedPath = parseAbsolutePath(absolutePath.value());

        if(!parsedPath.ok())
        {
            return failure<OpenPath>(parsedPath.status());
        }

        // Open roots directly because they have no parent components.
        if(parsedPath.value().components.empty())
        {
            auto root =
                openRootDirectory(parsedPath.value().root, shareMode, desiredAccess, symlinkPolicy == Types::SymlinkPolicy::DoNotFollow, fallback);

            if(!root.ok())
            {
                return failure<OpenPath>(root.status());
            }

            auto info = queryHandleInfo(root.value().handle.get());

            if(!info.ok())
            {
                return failure<OpenPath>(info.status());
            }

            if(!allowDirectory && info.value().kind == Types::EntryKind::Directory)
            {
                return failure<OpenPath>(ErrorCode::IsDirectory);
            }

            return Result<OpenPath>::success(OpenPath{.handle = std::move(root).value().handle, .info = std::move(info).value()});
        }

        // Resolve the parent with stable handles for strict symlink policies.
        auto parent = openParentStrict(parsedPath.value(), shareMode);

        if(!parent.ok())
        {
            return failure<OpenPath>(parent.status());
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

        auto finalHandle = openChildNative(
            parent.value().handle.get(),
            parsedPath.value().components.back(),
            desiredAccess,
            shareMode,
            FILE_OPEN,
            openOptions,
            FILE_ATTRIBUTE_NORMAL,
            fallback);

        if(!finalHandle.ok())
        {
            return failure<OpenPath>(finalHandle.status());
        }

        auto info = queryHandleInfo(finalHandle.value().handle.get());

        if(!info.ok())
        {
            return failure<OpenPath>(info.status());
        }

        if(!allowDirectory && info.value().kind == Types::EntryKind::Directory)
        {
            return failure<OpenPath>(ErrorCode::IsDirectory);
        }

        return Result<OpenPath>::success(OpenPath{.handle = std::move(finalHandle).value().handle, .info = std::move(info).value()});
    }

    /// @brief Retains every traversal handle so a multi-step operation cannot swap path components.
    [[nodiscard]] Result<StablePath> stabilizeExistingPath(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy, bool requireDirectory)
    {
        auto absolutePath = absoluteNativePath(path);

        if(!absolutePath.ok())
        {
            return failure<StablePath>(absolutePath.status());
        }

        // Keep one stable handle when ordinary symlink traversal is allowed.
        if(symlinkPolicy == Types::SymlinkPolicy::FollowAll)
        {
            auto opened = openExistingPath(path, symlinkPolicy, kQueryAccess, kShareWithoutDelete, true, ErrorCode::StatFailed);

            if(!opened.ok())
            {
                return failure<StablePath>(opened.status());
            }

            if(requireDirectory && opened.value().info.kind != Types::EntryKind::Directory)
            {
                return failure<StablePath>(ErrorCode::NotDirectory);
            }

            std::vector<UniqueHandle> handles;
            OpenPath openedValue = std::move(opened).value();
            Types::EntryInfo info = std::move(openedValue.info);
            handles.push_back(std::move(openedValue.handle));
            return Result<StablePath>::success(StablePath{.handles = std::move(handles), .info = std::move(info)});
        }

        auto parsedPath = parseAbsolutePath(absolutePath.value());

        if(!parsedPath.ok())
        {
            return failure<StablePath>(parsedPath.status());
        }

        std::vector<UniqueHandle> handles;
        const DWORD stableShare = kShareWithoutDelete;

        // Keep the root handle as the complete stable path.
        if(parsedPath.value().components.empty())
        {
            const bool doNotFollowRoot = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
            auto root = openRootDirectory(parsedPath.value().root, stableShare, kDirectoryTraversalAccess, doNotFollowRoot);

            if(!root.ok())
            {
                return failure<StablePath>(root.status());
            }

            auto info = queryHandleInfo(root.value().handle.get());

            if(!info.ok())
            {
                return failure<StablePath>(info.status());
            }

            if(requireDirectory && info.value().kind != Types::EntryKind::Directory)
            {
                return failure<StablePath>(ErrorCode::NotDirectory);
            }

            handles.push_back(std::move(root).value().handle);
            return Result<StablePath>::success(StablePath{.handles = std::move(handles), .info = std::move(info).value()});
        }

        // Retain each resolved component so later operations use the same ancestry.
        auto parent = openParentStrict(parsedPath.value(), stableShare, &handles);

        if(!parent.ok())
        {
            return failure<StablePath>(parent.status());
        }

        const bool doNotFollowFinal = symlinkPolicy == Types::SymlinkPolicy::DoNotFollow;
        auto finalHandle = openChild(handles.back().get(), parsedPath.value().components.back(), doNotFollowFinal, requireDirectory, stableShare);

        if(!finalHandle.ok())
        {
            return failure<StablePath>(finalHandle.status());
        }

        auto info = queryHandleInfo(finalHandle.value().handle.get());

        if(!info.ok())
        {
            return failure<StablePath>(info.status());
        }

        if(requireDirectory && info.value().kind != Types::EntryKind::Directory)
        {
            return failure<StablePath>(ErrorCode::NotDirectory);
        }

        handles.push_back(std::move(finalHandle).value().handle);
        return Result<StablePath>::success(StablePath{.handles = std::move(handles), .info = std::move(info).value()});
    }

} // namespace GameWIP::FileSystem::Platform::Win32
