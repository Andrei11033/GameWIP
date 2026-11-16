/// @file directories.cpp
/// @brief FileSystem directory creation, listing, and removal operations.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/filesystem_platform.h"

#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace GameWIP::FileSystem
{
    using namespace Detail;

    namespace
    {
        // List failures

        [[nodiscard]] Types::Directory::ListResult listDirectoryFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .entries = {}};
        }

        [[nodiscard]] Types::Directory::ListResult listDirectoryFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .entries = {}};
        }

        // Tree-removal failures

        [[nodiscard]] Types::Directory::RemoveTreeResult removeTreeFailure(IO::Types::Status status, std::uint64_t removedEntries = 0) noexcept
        {
            return {.status = std::move(status), .removedEntries = removedEntries};
        }

        [[nodiscard]] Types::Directory::RemoveTreeResult removeTreeFailure(ErrorCode code, std::uint64_t removedEntries = 0) noexcept
        {
            return {.status = IO::makeStatus(code), .removedEntries = removedEntries};
        }
    } // namespace

    IO::Types::Status createDirectory(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            return Detail::Platform::createDirectory(path, options);
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
            return Detail::Platform::createDirectories(path, options);
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

    Types::Directory::ListResult listDirectory(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
    {
        try
        {
            DirectoryCursor cursor;
            IO::Types::Status openStatus = cursor.open(path, options);

            if(!openStatus.ok())
            {
                return listDirectoryFailure(std::move(openStatus));
            }

            Types::Directory::ListResult result{.status = IO::successStatus()};

            while(true)
            {
                Types::Directory::CursorNextResult next = cursor.next();

                if(!next.status.ok())
                {
                    result.status = std::move(next.status);
                    return result;
                }

                if(!next.hasEntry)
                {
                    return result;
                }
                result.entries.push_back(std::move(next.entry));
            }
        }
        catch(const std::bad_alloc &)
        {
            return listDirectoryFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return listDirectoryFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return IO::makeStatus(ErrorCode::Unsupported);
            }

            return Detail::Platform::removeEmptyDirectory(path, options);
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

    Types::Directory::RemoveTreeResult removeDirectoryTree(const Types::Path &path, const Types::Directory::RemoveTreeOptions &options) noexcept
    {
        try
        {
            // Validate the request and resolve the initial entry.
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return removeTreeFailure(ErrorCode::InvalidArgument);
            }

            Detail::Platform::EntryQueryResult existing = queryEntry(path, options.symlinkPolicy);

            if(!existing.status.ok())
            {
                if(existing.status.code == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return {.status = IO::successStatus(), .removedEntries = 0};
                }
                return removeTreeFailure(std::move(existing.status));
            }

            if(existing.info.kind != Types::EntryKind::Directory)
            {
                return removeTreeFailure(ErrorCode::NotDirectory);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return removeTreeFailure(ErrorCode::Unsupported);
            }

            const Types::BoolResult rootPath = Detail::Platform::isRootPath(path);

            if(!rootPath.status.ok())
            {
                return removeTreeFailure(rootPath.status);
            }

            if(options.maxEntries == 0)
            {
                return removeTreeFailure(ErrorCode::SizeLimitExceeded);
            }

            struct RemovalFrame
            {
                // Entry being visited
                Types::Path path;
                Types::EntryInfo info{};

                // Active directory enumeration
                std::unique_ptr<Detail::DirectoryCursorState> cursor;
            };

            // Keep only active ancestry so retained state grows with depth, not total entry count.
            std::vector<RemovalFrame> pending;
            pending.push_back(RemovalFrame{.path = path, .info = existing.info});

            std::uint64_t removedEntries = 0;

            while(!pending.empty())
            {
                // Enforce the limit before removing the next entry.
                if(removedEntries >= options.maxEntries && !(rootPath.value && pending.size() == 1))
                {
                    return removeTreeFailure(ErrorCode::SizeLimitExceeded, removedEntries);
                }

                RemovalFrame &current = pending.back();

                // Descend into the next child or finish this directory.
                if(current.info.kind == Types::EntryKind::Directory)
                {
                    if(!current.cursor)
                    {
                        // Open the root once and children relative to their retained parent.
                        Detail::Platform::DirectoryCursorOpenResult opened;

                        if(pending.size() == 1)
                        {
                            opened = Detail::Platform::openDirectoryCursor(current.path, Types::SymlinkPolicy::DoNotFollow);
                        }
                        else
                        {
                            const RemovalFrame &parent = pending[pending.size() - 2];
                            opened = Detail::Platform::openChildDirectoryCursor(*parent.cursor, current.path, Types::SymlinkPolicy::DoNotFollow);
                        }

                        if(!opened.status.ok())
                        {
                            return removeTreeFailure(opened.status, removedEntries);
                        }

                        current.cursor = std::move(opened.state);
                    }

                    Detail::Platform::DirectoryCursorNextResult child = Detail::Platform::readDirectoryCursor(*current.cursor);

                    if(!child.status.ok())
                    {
                        return removeTreeFailure(child.status, removedEntries);
                    }

                    if(child.hasEntry)
                    {
                        pending.push_back(RemovalFrame{.path = std::move(child.entry.path), .info = child.entry.info});
                        continue;
                    }

                    current.cursor.reset();
                }

                // Filesystem roots contain removable entries but cannot be removed themselves.
                if(rootPath.value && pending.size() == 1)
                {
                    pending.pop_back();
                    continue;
                }

                // Remove the leaf or the now-empty directory.
                const IO::Types::Status removeStatus =
                    current.info.kind == Types::EntryKind::Directory
                        ? removeEmptyDirectory(
                              current.path,
                              Types::RemoveOptions{.succeedIfMissing = false, .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow})
                        : removeFile(
                              current.path,
                              Types::RemoveOptions{.succeedIfMissing = false, .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow});

                if(!removeStatus.ok())
                {
                    return removeTreeFailure(removeStatus, removedEntries);
                }

                // Record progress only after removal succeeds.
                pending.pop_back();
                ++removedEntries;
            }

            return {.status = IO::successStatus(), .removedEntries = removedEntries};
        }
        catch(const std::bad_alloc &)
        {
            return removeTreeFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return removeTreeFailure(ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::FileSystem
