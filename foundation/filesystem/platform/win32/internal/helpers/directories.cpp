/// @file directories.cpp
/// @brief Win32 FileSystem native directory operations.

#include "filesystem/platform/win32/internal/helpers.h"

namespace GameWIP::FileSystem::Detail::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Directory enumeration helpers
    /// ------------------------------------------------------------

    /// @brief Filters synthetic dot entries from Win32 directory enumeration.
    [[nodiscard]] bool isDotDirectoryEntry(std::wstring_view name) noexcept
    {
        return name == L"." || name == L"..";
    }

    /// @brief Appends a wildcard component to an extended-length directory path.
    [[nodiscard]] std::wstring searchWildcardPath(const std::wstring &path)
    {
        std::wstring result = path;

        if(!result.empty() && !isSeparator(result.back()))
        {
            result.push_back(L'\\');
        }
        result.push_back(L'*');
        return result;
    }

    /// ------------------------------------------------------------
    /// Symlink-aware directory creation
    /// ------------------------------------------------------------

    /// @brief Creates one final directory through a stable parent handle.
    [[nodiscard]] IO::Types::Status createDirectoryStrict(const Types::Path &path, const Types::Directory::CreateOptions &options)
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
            return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
        }
        EntryQueryResult existing = queryStrictPath(parsedPath, options.symlinkPolicy);

        if(existing.status.ok())
        {
            if(existing.info.kind == Types::EntryKind::Directory)
            {
                return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
            }
            return IO::makeStatus(ErrorCode::AlreadyExists);
        }

        if(existing.status.code != ErrorCode::NotFound)
        {
            return std::move(existing.status);
        }

        HandleResult parent = openParentStrict(parsedPath);

        if(!parent.status.ok())
        {
            return std::move(parent.status);
        }

        HandleResult created = openChildNative(
            parent.handle.get(),
            parsedPath.components.back(),
            kDirectoryTraversalAccess,
            kShareAll,
            FILE_CREATE,
            kOpenOptionsBase | FILE_DIRECTORY_FILE,
            FILE_ATTRIBUTE_DIRECTORY,
            ErrorCode::DirectoryCreateFailed);
        return std::move(created.status);
    }

    /// @brief Creates missing components one at a time while retaining strict traversal handles.
    [[nodiscard]] IO::Types::Status createDirectoriesStrict(const Types::Path &path, const Types::Directory::CreateOptions &options)
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
            return options.succeedIfAlreadyExists ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
        }
        HandleResult current = openRootDirectory(parsedPath.root);

        if(!current.status.ok())
        {
            return std::move(current.status);
        }

        bool createdAny = false;

        for(std::size_t index = 0; index < parsedPath.components.size(); ++index)
        {
            const bool finalComponent = index + 1 == parsedPath.components.size();
            const bool followThisComponent = finalComponent && options.symlinkPolicy == Types::SymlinkPolicy::FollowFinal;
            HandleResult child = openChild(current.handle.get(), parsedPath.components[index], !followThisComponent, true);

            if(!child.status.ok())
            {
                if(child.status.code != ErrorCode::NotFound)
                {
                    return std::move(child.status);
                }
                child = openChildNative(
                    current.handle.get(),
                    parsedPath.components[index],
                    kDirectoryTraversalAccess,
                    kShareAll,
                    FILE_CREATE,
                    kOpenOptionsBase | FILE_DIRECTORY_FILE,
                    FILE_ATTRIBUTE_DIRECTORY,
                    ErrorCode::DirectoryCreateFailed);

                if(!child.status.ok())
                {
                    return std::move(child.status);
                }
                createdAny = true;
            }

            EntryQueryResult childInfo = queryHandleInfo(child.handle.get());

            if(!childInfo.status.ok())
            {
                return std::move(childInfo.status);
            }

            if(childInfo.info.kind == Types::EntryKind::Symlink && !followThisComponent)
            {
                return symlinkPolicyRejectedStatus();
            }

            if(childInfo.info.kind != Types::EntryKind::Directory)
            {
                return IO::makeStatus(ErrorCode::AlreadyExists);
            }
            current.handle = std::move(child.handle);
        }

        return (createdAny || options.succeedIfAlreadyExists) ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
    }

    /// ------------------------------------------------------------
    /// Directory cursor helpers
    /// ------------------------------------------------------------

    /// Converts one native enumeration record into a platform-neutral directory entry.
    [[nodiscard]] DirectoryCursorNextResult directoryCursorEntry(Detail::DirectoryCursorState &state, const WIN32_FIND_DATAW &findData)
    {
        const std::wstring childName{findData.cFileName};
        const Types::Path childPath = state.directoryPath / std::filesystem::path{childName};

        if(state.stableHandles.empty())
        {
            return {.status = IO::makeStatus(ErrorCode::DirectoryListFailed)};
        }

        // Query relative to the retained directory handle. This preserves strict traversal and
        // avoids reopening the complete ancestry once per child.
        const HANDLE parentHandle = static_cast<HANDLE>(state.stableHandles.back());
        HandleResult childHandle = openChild(parentHandle, childName, state.symlinkPolicy == Types::SymlinkPolicy::DoNotFollow, false);

        if(!childHandle.status.ok())
        {
            return {.status = std::move(childHandle.status)};
        }
        EntryQueryResult child = queryHandleInfo(childHandle.handle.get());

        if(!child.status.ok())
        {
            return {.status = std::move(child.status)};
        }

        return {
            .status = IO::successStatus(),
            .entry = Types::Directory::Entry{.path = childPath, .info = child.info},
            .hidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0,
            .hasEntry = true};
    }

    /// Starts native enumeration after the caller has stabilized the directory path.
    [[nodiscard]] DirectoryCursorOpenResult startDirectoryCursor(std::unique_ptr<Detail::DirectoryCursorState> state)
    {
        WidePathResult absolutePath = absoluteNativePath(state->directoryPath);

        if(!absolutePath.status.ok())
        {
            return {.status = std::move(absolutePath.status)};
        }

        // Start native enumeration and skip synthetic dot entries.
        WIN32_FIND_DATAW findData{};
        UniqueFindHandle findHandle{FindFirstFileW(searchWildcardPath(absolutePath.path).c_str(), &findData)};

        if(!findHandle.isValid())
        {
            const DWORD error = GetLastError();

            if(error == ERROR_FILE_NOT_FOUND)
            {
                state->finished = true;
            }
            else
            {
                return {.status = makeWin32Status(error, ErrorCode::DirectoryListFailed)};
            }
        }
        else
        {
            while(isDotDirectoryEntry(findData.cFileName))
            {
                if(FindNextFileW(findHandle.get(), &findData) != FALSE)
                {
                    continue;
                }

                const DWORD error = GetLastError();

                if(error != ERROR_NO_MORE_FILES)
                {
                    return {.status = makeWin32Status(error, ErrorCode::DirectoryListFailed)};
                }
                state->finished = true;
                break;
            }

            if(!state->finished)
            {
                DirectoryCursorNextResult first = directoryCursorEntry(*state, findData);

                if(!first.status.ok())
                {
                    return {.status = std::move(first.status)};
                }
                state->bufferedEntry = std::move(first.entry);
                state->bufferedEntryHidden = first.hidden;
                state->hasBufferedEntry = true;
            }
        }

        // Retain the first visible entry and transfer handle ownership to the cursor.
        if(findHandle.isValid())
        {
            state->nativeFindHandle = findHandle.release();
        }

        return {.status = IO::successStatus(), .state = std::move(state)};
    }

    /// Opens a root cursor while retaining the complete path traversal chain.
    [[nodiscard]] DirectoryCursorOpenResult openDirectoryCursorImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
    {
        // Stabilize the requested directory before creating cursor state.
        StablePathResult stablePath = stabilizeExistingPath(path, symlinkPolicy, true);

        if(!stablePath.status.ok())
        {
            return {.status = std::move(stablePath.status)};
        }

        auto state = std::make_unique<Detail::DirectoryCursorState>();
        state->directoryPath = path;
        state->symlinkPolicy = symlinkPolicy;
        state->stableHandles.reserve(stablePath.handles.size());

        for(UniqueHandle &handle : stablePath.handles)
        {
            state->stableHandles.push_back(handle.release());
        }

        return startDirectoryCursor(std::move(state));
    }

    /// Opens a child from its stabilized parent so active nested cursors retain one new handle per level.
    [[nodiscard]] DirectoryCursorOpenResult openChildDirectoryCursorImpl(
        const Detail::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy)
    {
        if(symlinkPolicy != Types::SymlinkPolicy::DoNotFollow || parent.stableHandles.empty())
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        // Open the child relative to the parent's retained directory handle.
        const std::wstring childName = path.filename().wstring();

        if(childName.empty())
        {
            return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
        }

        const HANDLE parentHandle = static_cast<HANDLE>(parent.stableHandles.back());
        HandleResult childHandle = openChild(parentHandle, childName, true, true, kShareWithoutDelete);

        if(!childHandle.status.ok())
        {
            return {.status = std::move(childHandle.status)};
        }

        auto state = std::make_unique<Detail::DirectoryCursorState>();
        state->directoryPath = path;
        state->symlinkPolicy = symlinkPolicy;
        state->stableHandles.push_back(childHandle.handle.release());
        return startDirectoryCursor(std::move(state));
    }

    /// Advances one cursor without collecting sibling entries.
    [[nodiscard]] DirectoryCursorNextResult readDirectoryCursorImpl(Detail::DirectoryCursorState &state)
    {
        if(state.hasBufferedEntry)
        {
            state.hasBufferedEntry = false;
            return {.status = IO::successStatus(), .entry = std::move(state.bufferedEntry), .hidden = state.bufferedEntryHidden, .hasEntry = true};
        }

        if(state.finished)
        {
            return {.status = IO::successStatus()};
        }
        const HANDLE findHandle = static_cast<HANDLE>(state.nativeFindHandle);
        WIN32_FIND_DATAW findData{};

        while(FindNextFileW(findHandle, &findData) != FALSE)
        {
            if(!isDotDirectoryEntry(findData.cFileName))
            {
                return directoryCursorEntry(state, findData);
            }
        }

        const DWORD error = GetLastError();

        if(error == ERROR_NO_MORE_FILES)
        {
            state.finished = true;
            return {.status = IO::successStatus()};
        }

        return {.status = makeWin32Status(error, ErrorCode::DirectoryListFailed)};
    }

    /// Enumerates direct children and applies public filtering and count policy.
    [[nodiscard]] Types::Directory::ListResult listDirectoryImpl(const Types::Path &path, const Types::Directory::ListOptions &options)
    {
        // Reuse cursor traversal and apply filters as entries arrive.
        DirectoryCursorOpenResult opened = openDirectoryCursorImpl(path, options.symlinkPolicy);

        if(!opened.status.ok())
        {
            return {.status = std::move(opened.status)};
        }

        Types::Directory::ListResult result{.status = IO::successStatus()};

        while(true)
        {
            DirectoryCursorNextResult next = readDirectoryCursorImpl(*opened.state);

            if(!next.status.ok())
            {
                result.status = std::move(next.status);
                return result;
            }

            if(!next.hasEntry)
            {
                return result;
            }

            if((!options.includeHidden && next.hidden) || !includeEntryKind(next.entry.info.kind, options))
            {
                continue;
            }

            if(result.entries.size() >= options.maxEntries)
            {
                result.status = IO::makeStatus(ErrorCode::SizeLimitExceeded);
                return result;
            }
            result.entries.push_back(std::move(next.entry));
        }
    }

} // namespace GameWIP::FileSystem::Detail::Platform::Win32
