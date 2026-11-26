/// @file directories.cpp
/// @brief Win32 FileSystem native directory operations.

#include "filesystem/platform/win32/internal/helpers.h"

namespace GameWIP::FileSystem::Platform::Win32
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
    [[nodiscard]] Result<void> createDirectoryStrict(const Types::Path &path, const Types::Directory::CreateOptions &options)
    {
        // Resolve and validate the target path.
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
            return options.succeedIfAlreadyExists ? success() : failure<void>(ErrorCode::AlreadyExists);
        }

        // Reuse an existing directory or propagate a non-missing error.
        auto existing = queryStrictPath(parsedPath.value(), options.symlinkPolicy);

        if(existing.ok())
        {
            if(existing.value().kind == Types::EntryKind::Directory)
            {
                return options.succeedIfAlreadyExists ? success() : failure<void>(ErrorCode::AlreadyExists);
            }

            return failure<void>(ErrorCode::AlreadyExists);
        }

        if(existing.status().code() != ErrorCode::NotFound)
        {
            return failure<void>(existing.status());
        }

        // Create the target relative to its stable parent.
        auto parent = openParentStrict(parsedPath.value());

        if(!parent.ok())
        {
            return failure<void>(parent.status());
        }

        auto created = openChildNative(
            parent.value().handle.get(),
            parsedPath.value().components.back(),
            kDirectoryTraversalAccess,
            kShareAll,
            FILE_CREATE,
            kOpenOptionsBase | FILE_DIRECTORY_FILE,
            FILE_ATTRIBUTE_DIRECTORY,
            ErrorCode::DirectoryCreateFailed);

        if(!created.ok())
        {
            return failure<void>(created.status());
        }

        return success();
    }

    /// @brief Creates missing components one at a time while retaining strict traversal handles.
    [[nodiscard]] Result<void> createDirectoriesStrict(const Types::Path &path, const Types::Directory::CreateOptions &options)
    {
        // Resolve the target path.
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
            return options.succeedIfAlreadyExists ? success() : failure<void>(ErrorCode::AlreadyExists);
        }

        // Start traversal from the root and create missing components.
        auto current = openRootDirectory(parsedPath.value().root);

        if(!current.ok())
        {
            return failure<void>(current.status());
        }

        bool createdAny = false;

        for(std::size_t index = 0; index < parsedPath.value().components.size(); ++index)
        {
            const bool finalComponent = index + 1 == parsedPath.value().components.size();
            const bool followThisComponent = finalComponent && options.symlinkPolicy == Types::SymlinkPolicy::FollowFinal;
            auto child = openChild(current.value().handle.get(), parsedPath.value().components[index], !followThisComponent, true);

            if(!child.ok())
            {
                if(child.status().code() != ErrorCode::NotFound)
                {
                    return failure<void>(child.status());
                }

                child = openChildNative(
                    current.value().handle.get(),
                    parsedPath.value().components[index],
                    kDirectoryTraversalAccess,
                    kShareAll,
                    FILE_CREATE,
                    kOpenOptionsBase | FILE_DIRECTORY_FILE,
                    FILE_ATTRIBUTE_DIRECTORY,
                    ErrorCode::DirectoryCreateFailed);

                if(!child.ok())
                {
                    return failure<void>(child.status());
                }

                createdAny = true;
            }

            auto childInfo = queryHandleInfo(child.value().handle.get());

            if(!childInfo.ok())
            {
                return failure<void>(childInfo.status());
            }

            if(childInfo.value().kind == Types::EntryKind::Symlink && !followThisComponent)
            {
                return failure<void>(symlinkPolicyRejectedStatus());
            }

            if(childInfo.value().kind != Types::EntryKind::Directory)
            {
                return failure<void>(ErrorCode::AlreadyExists);
            }

            current.value().handle = std::move(child).value().handle;
        }

        return (createdAny || options.succeedIfAlreadyExists) ? success() : failure<void>(ErrorCode::AlreadyExists);
    }

    /// ------------------------------------------------------------
    /// Directory cursor helpers
    /// ------------------------------------------------------------

    /// Converts one native enumeration record into a platform-neutral directory entry.
    [[nodiscard]] Result<Platform::DirectoryCursorEntry> directoryCursorEntry(
        Internal::DirectoryCursorState &state,
        const WIN32_FIND_DATAW &findData)
    {
        const std::wstring childName{findData.cFileName};
        const Types::Path childPath = state.directoryPath / std::filesystem::path{childName};

        if(state.stableHandles.empty())
        {
            return failure<Platform::DirectoryCursorEntry>(ErrorCode::DirectoryListFailed);
        }

        // Query relative to the retained directory handle. This preserves strict traversal and
        // avoids reopening the complete ancestry once per child.
        const HANDLE parentHandle = static_cast<HANDLE>(state.stableHandles.back());
        auto childHandle = openChild(parentHandle, childName, state.symlinkPolicy == Types::SymlinkPolicy::DoNotFollow, false);

        if(!childHandle.ok())
        {
            return failure<Platform::DirectoryCursorEntry>(childHandle.status());
        }

        auto child = queryHandleInfo(childHandle.value().handle.get());

        if(!child.ok())
        {
            return failure<Platform::DirectoryCursorEntry>(child.status());
        }

        Platform::DirectoryCursorEntry entry{
            .entry = Types::Directory::Entry{.path = childPath, .info = std::move(child).value()},
            .hidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0};
        return Result<Platform::DirectoryCursorEntry>::success(std::move(entry));
    }

    /// Starts native enumeration after the caller has stabilized the directory path.
    [[nodiscard]] Result<std::unique_ptr<Internal::DirectoryCursorState>>
    startDirectoryCursor(std::unique_ptr<Internal::DirectoryCursorState> state)
    {
        auto absolutePath = absoluteNativePath(state->directoryPath);

        if(!absolutePath.ok())
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(absolutePath.status());
        }

        // Start native enumeration and skip synthetic dot entries.
        WIN32_FIND_DATAW findData{};
        UniqueFindHandle findHandle{FindFirstFileW(searchWildcardPath(absolutePath.value()).c_str(), &findData)};

        if(!findHandle.isValid())
        {
            const DWORD error = GetLastError();

            if(error == ERROR_FILE_NOT_FOUND)
            {
                state->finished = true;
            }
            else
            {
                return failure<std::unique_ptr<Internal::DirectoryCursorState>>(
                    makeWin32Status(error, ErrorCode::DirectoryListFailed));
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
                    return failure<std::unique_ptr<Internal::DirectoryCursorState>>(
                        makeWin32Status(error, ErrorCode::DirectoryListFailed));
                }
                state->finished = true;
                break;
            }

            if(!state->finished)
            {
                auto first = directoryCursorEntry(*state, findData);

                if(!first.ok())
                {
                    return failure<std::unique_ptr<Internal::DirectoryCursorState>>(first.status());
                }

                Platform::DirectoryCursorEntry entry = std::move(first).value();
                state->bufferedEntry = std::move(entry.entry);
                state->bufferedEntryHidden = entry.hidden;
                state->hasBufferedEntry = true;
            }
        }

        // Retain the first visible entry and transfer handle ownership to the cursor.
        if(findHandle.isValid())
        {
            state->nativeFindHandle = findHandle.release();
        }

        return Result<std::unique_ptr<Internal::DirectoryCursorState>>::success(std::move(state));
    }

    /// Opens a root cursor while retaining the complete path traversal chain.
    [[nodiscard]] Result<std::unique_ptr<Internal::DirectoryCursorState>>
    openDirectoryCursorImpl(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
    {
        // Stabilize the requested directory before creating cursor state.
        auto stablePath = stabilizeExistingPath(path, symlinkPolicy, true);

        if(!stablePath.ok())
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(stablePath.status());
        }

        StablePath stableValue = std::move(stablePath).value();
        auto state = std::make_unique<Internal::DirectoryCursorState>();
        state->directoryPath = path;
        state->symlinkPolicy = symlinkPolicy;
        state->stableHandles.reserve(stableValue.handles.size());

        for(UniqueHandle &handle : stableValue.handles)
        {
            state->stableHandles.push_back(handle.release());
        }

        return startDirectoryCursor(std::move(state));
    }

    /// Opens a child from its stabilized parent so active nested cursors retain one new handle per level.
    [[nodiscard]] Result<std::unique_ptr<Internal::DirectoryCursorState>> openChildDirectoryCursorImpl(
        const Internal::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy)
    {
        if(symlinkPolicy != Types::SymlinkPolicy::DoNotFollow || parent.stableHandles.empty())
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::InvalidArgument);
        }

        // Open the child relative to the parent's retained directory handle.
        const std::wstring childName = path.filename().wstring();

        if(childName.empty())
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::InvalidArgument);
        }

        const HANDLE parentHandle = static_cast<HANDLE>(parent.stableHandles.back());
        auto childHandle = openChild(parentHandle, childName, true, true, kShareWithoutDelete);

        if(!childHandle.ok())
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(childHandle.status());
        }

        auto state = std::make_unique<Internal::DirectoryCursorState>();
        state->directoryPath = path;
        state->symlinkPolicy = symlinkPolicy;
        state->stableHandles.push_back(std::move(childHandle).value().handle.release());
        return startDirectoryCursor(std::move(state));
    }

    /// Advances one cursor without collecting sibling entries.
    [[nodiscard]] Result<std::optional<Platform::DirectoryCursorEntry>> readDirectoryCursorImpl(Internal::DirectoryCursorState &state)
    {
        if(state.hasBufferedEntry)
        {
            state.hasBufferedEntry = false;
            Platform::DirectoryCursorEntry entry{
                .entry = std::move(state.bufferedEntry),
                .hidden = state.bufferedEntryHidden};
            return Result<std::optional<Platform::DirectoryCursorEntry>>::success(std::move(entry));
        }

        if(state.finished)
        {
            return Result<std::optional<Platform::DirectoryCursorEntry>>::success(std::nullopt);
        }

        const HANDLE findHandle = static_cast<HANDLE>(state.nativeFindHandle);
        WIN32_FIND_DATAW findData{};

        while(FindNextFileW(findHandle, &findData) != FALSE)
        {
            if(!isDotDirectoryEntry(findData.cFileName))
            {
                auto entry = directoryCursorEntry(state, findData);

                if(!entry.ok())
                {
                    return failure<std::optional<Platform::DirectoryCursorEntry>>(entry.status());
                }

                return Result<std::optional<Platform::DirectoryCursorEntry>>::success(std::move(entry).value());
            }
        }

        const DWORD error = GetLastError();

        if(error == ERROR_NO_MORE_FILES)
        {
            state.finished = true;
            return Result<std::optional<Platform::DirectoryCursorEntry>>::success(std::nullopt);
        }

        return failure<std::optional<Platform::DirectoryCursorEntry>>(makeWin32Status(error, ErrorCode::DirectoryListFailed));
    }

    /// Enumerates direct children and applies public filtering and count policy.
    [[nodiscard]] Result<std::vector<Types::Directory::Entry>>
    listDirectoryImpl(const Types::Path &path, const Types::Directory::ListOptions &options)
    {
        // Reuse cursor traversal and apply filters as entries arrive.
        auto opened = openDirectoryCursorImpl(path, options.symlinkPolicy);

        if(!opened.ok())
        {
            return failure<std::vector<Types::Directory::Entry>>(opened.status());
        }

        std::vector<Types::Directory::Entry> entries;

        while(true)
        {
            auto next = readDirectoryCursorImpl(*opened.value());

            if(!next.ok())
            {
                return Result<std::vector<Types::Directory::Entry>>::failure(next.status(), std::move(entries));
            }

            if(!next.value())
            {
                return Result<std::vector<Types::Directory::Entry>>::success(std::move(entries));
            }

            auto nextValue = std::move(next).value();
            Platform::DirectoryCursorEntry entry = std::move(*nextValue);

            if((!options.includeHidden && entry.hidden) || !includeEntryKind(entry.entry.info.kind, options))
            {
                continue;
            }

            if(entries.size() >= options.maxEntries)
            {
                return Result<std::vector<Types::Directory::Entry>>::failure(
                    Status(ErrorCode::SizeLimitExceeded), std::move(entries));
            }

            entries.push_back(std::move(entry.entry));
        }
    }

} // namespace GameWIP::FileSystem::Platform::Win32
