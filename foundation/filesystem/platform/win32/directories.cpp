/// @file directories.cpp
/// @brief Win32 FileSystem directory operations.

#include "filesystem/internal/filesystem_platform.h"
#include "filesystem/platform/win32/internal/helpers.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem::Detail::Platform
{
    using namespace Win32;

    /// ------------------------------------------------------------
    /// Directory operations
    /// ------------------------------------------------------------

    IO::Types::Status createDirectory(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return createDirectoryStrict(path, options);
            }
            EntryQueryResult existing = queryEntryImpl(path, Types::SymlinkPolicy::FollowAll);

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
            WidePathResult nativePath = absoluteNativePath(path);

            if(!nativePath.status.ok())
            {
                return std::move(nativePath.status);
            }

            if(CreateDirectoryW(nativePath.path.c_str(), nullptr) == FALSE)
            {
                return makeLastErrorStatus(ErrorCode::DirectoryCreateFailed);
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

    IO::Types::Status createDirectories(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::FollowAll)
            {
                return createDirectoriesStrict(path, options);
            }
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
            Types::Path current = std::filesystem::path{parsedPath.root};
            bool createdAny = false;

            for(const std::wstring &component : parsedPath.components)
            {
                current /= std::filesystem::path{component};
                EntryQueryResult existing = queryEntryImpl(current, Types::SymlinkPolicy::FollowAll);

                if(existing.status.ok())
                {
                    if(existing.info.kind != Types::EntryKind::Directory)
                    {
                        return IO::makeStatus(ErrorCode::AlreadyExists);
                    }
                    continue;
                }

                if(existing.status.code != ErrorCode::NotFound)
                {
                    return std::move(existing.status);
                }
                WidePathResult nativeCurrent = absoluteNativePath(current);

                if(!nativeCurrent.status.ok())
                {
                    return std::move(nativeCurrent.status);
                }

                if(CreateDirectoryW(nativeCurrent.path.c_str(), nullptr) == FALSE)
                {
                    return makeLastErrorStatus(ErrorCode::DirectoryCreateFailed);
                }
                createdAny = true;
            }

            return (createdAny || options.succeedIfAlreadyExists) ? IO::successStatus() : IO::makeStatus(ErrorCode::AlreadyExists);
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

    Types::BoolResult isRootPath(const Types::Path &path) noexcept
    {
        try
        {
            WidePathResult absolutePath = absoluteNativePath(path);

            if(!absolutePath.status.ok())
            {
                return {.status = std::move(absolutePath.status)};
            }

            ParsedPathResult parsedPath = parseAbsolutePath(absolutePath.path);

            if(!parsedPath.status.ok())
            {
                return {.status = std::move(parsedPath.status)};
            }

            return {.status = IO::successStatus(), .value = parsedPath.components.empty()};
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

    Types::Directory::ListResult listDirectory(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return listDirectoryImpl(path, options);
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

    DirectoryCursorOpenResult openDirectoryCursor(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return openDirectoryCursorImpl(path, symlinkPolicy);
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

    DirectoryCursorNextResult readDirectoryCursor(Detail::DirectoryCursorState &state) noexcept
    {
        try
        {
            return readDirectoryCursorImpl(state);
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

    DirectoryCursorOpenResult openChildDirectoryCursor(
        const Detail::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(symlinkPolicy))
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
            return openChildDirectoryCursorImpl(parent, path, symlinkPolicy);
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

} // namespace GameWIP::FileSystem::Detail::Platform
