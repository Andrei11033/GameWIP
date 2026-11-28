/// @file directories.cpp
/// @brief Win32 FileSystem directory operations.

#include "filesystem/internal/platform.h"
#include "filesystem/platform/win32/internal/helpers.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem::Platform
{
    using namespace Internal;
    using namespace Win32;

    /// ------------------------------------------------------------
    /// Directory operations
    /// ------------------------------------------------------------

    Result<void> createDirectory(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return createDirectoryStrict(path, options);
            }

            auto existing = queryEntryImpl(path, Types::SymlinkPolicy::FollowAll);

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

            auto nativePath = absoluteNativePath(path);

            if(!nativePath.ok())
            {
                return failure<void>(nativePath.status());
            }

            if(CreateDirectoryW(nativePath.value().c_str(), nullptr) == FALSE)
            {
                return failure<void>(makeLastErrorStatus(ErrorCode::DirectoryCreateFailed));
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

    Result<void> createDirectories(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return failure<void>(ErrorCode::InvalidArgument);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return createDirectoriesStrict(path, options);
            }

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

            Types::Path current = std::filesystem::path{parsedPath.value().root};
            bool createdAny = false;

            for(const std::wstring &component : parsedPath.value().components)
            {
                current /= std::filesystem::path{component};
                auto existing = queryEntryImpl(current, Types::SymlinkPolicy::FollowAll);

                if(existing.ok())
                {
                    if(existing.value().kind != Types::EntryKind::Directory)
                    {
                        return failure<void>(ErrorCode::AlreadyExists);
                    }
                    continue;
                }

                if(existing.status().code() != ErrorCode::NotFound)
                {
                    return failure<void>(existing.status());
                }

                auto nativeCurrent = absoluteNativePath(current);

                if(!nativeCurrent.ok())
                {
                    return failure<void>(nativeCurrent.status());
                }

                if(CreateDirectoryW(nativeCurrent.value().c_str(), nullptr) == FALSE)
                {
                    return failure<void>(makeLastErrorStatus(ErrorCode::DirectoryCreateFailed));
                }

                createdAny = true;
            }

            return (createdAny || options.succeedIfAlreadyExists) ? success() : failure<void>(ErrorCode::AlreadyExists);
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

    Result<bool> isRootPath(const Types::Path &path) noexcept
    {
        try
        {
            auto absolutePath = absoluteNativePath(path);

            if(!absolutePath.ok())
            {
                return failure<bool>(absolutePath.status());
            }

            auto parsedPath = parseAbsolutePath(absolutePath.value());

            if(!parsedPath.ok())
            {
                return failure<bool>(parsedPath.status());
            }

            return Result<bool>::success(parsedPath.value().components.empty());
        }
        catch(const std::bad_alloc &)
        {
            return failure<bool>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<bool>(ErrorCode::Unknown);
        }
    }

    Result<std::vector<Types::Directory::Entry>> listDirectory(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return failure<std::vector<Types::Directory::Entry>>(ErrorCode::InvalidArgument);
            }

            return listDirectoryImpl(path, options);
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::vector<Types::Directory::Entry>>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::vector<Types::Directory::Entry>>(ErrorCode::Unknown);
        }
    }

    Result<std::unique_ptr<Internal::DirectoryCursorState>> openDirectoryCursor(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::InvalidArgument);
            }

            return openDirectoryCursorImpl(path, symlinkPolicy);
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::Unknown);
        }
    }

    Result<std::optional<DirectoryCursorEntry>> readDirectoryCursor(Internal::DirectoryCursorState &state) noexcept
    {
        try
        {
            return readDirectoryCursorImpl(state);
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::optional<DirectoryCursorEntry>>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::optional<DirectoryCursorEntry>>(ErrorCode::Unknown);
        }
    }

    Result<std::unique_ptr<Internal::DirectoryCursorState>> openChildDirectoryCursor(
        const Internal::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::InvalidArgument);
            }

            return openChildDirectoryCursorImpl(parent, path, symlinkPolicy);
        }
        catch(const std::bad_alloc &)
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return failure<std::unique_ptr<Internal::DirectoryCursorState>>(ErrorCode::Unknown);
        }
    }

} // namespace GameWIP::FileSystem::Platform
