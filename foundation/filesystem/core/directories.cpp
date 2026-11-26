/// @file directories.cpp
/// @brief FileSystem directory creation, listing, and removal operations.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace GameWIP::FileSystem
{
    using namespace Internal;

    namespace
    {
        // List failures

        [[nodiscard]] GameWIP::Status::Types::Result<std::vector<Types::Directory::Entry>> listDirectoryFailure(
            GameWIP::Status::Types::Status status,
            std::vector<Types::Directory::Entry> entries = {}) noexcept
        {
            return GameWIP::Status::Types::Result<std::vector<Types::Directory::Entry>>::failure(std::move(status), std::move(entries));
        }

        [[nodiscard]] GameWIP::Status::Types::Result<std::vector<Types::Directory::Entry>> listDirectoryFailure(
            ErrorCode code,
            std::vector<Types::Directory::Entry> entries = {}) noexcept
        {
            return listDirectoryFailure(GameWIP::Status::Types::Status(code), std::move(entries));
        }

        // Tree-removal failures

        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> removeTreeFailure(
            GameWIP::Status::Types::Status status,
            std::uint64_t removedEntries = 0) noexcept
        {
            return GameWIP::Status::Types::Result<std::uint64_t>::failure(std::move(status), removedEntries);
        }

        [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> removeTreeFailure(ErrorCode code, std::uint64_t removedEntries = 0) noexcept
        {
            return removeTreeFailure(GameWIP::Status::Types::Status(code), removedEntries);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Directory creation and listing
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> createDirectory(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            return Platform::createDirectory(path, options);
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<void> createDirectories(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept
    {
        try
        {
            return Platform::createDirectories(path, options);
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<std::vector<Types::Directory::Entry>>
    listDirectory(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept
    {
        std::vector<Types::Directory::Entry> entries;

        try
        {
            DirectoryCursor cursor;
            auto openResult = cursor.open(path, options);

            if(!openResult.ok())
            {
                return listDirectoryFailure(openResult.status());
            }

            while(true)
            {
                auto next = cursor.next();

                if(!next.ok())
                {
                    return listDirectoryFailure(next.status(), std::move(entries));
                }

                if(!next.value())
                {
                    return GameWIP::Status::Types::Result<std::vector<Types::Directory::Entry>>::success(std::move(entries));
                }

                entries.push_back(std::move(*next.value()));
            }
        }
        catch(const std::bad_alloc &)
        {
            return listDirectoryFailure(ErrorCode::OutOfMemory, std::move(entries));
        }
        catch(...)
        {
            return listDirectoryFailure(ErrorCode::Unknown, std::move(entries));
        }
    }

    /// ------------------------------------------------------------
    /// Directory removal
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept
    {
        try
        {
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::InvalidArgument));
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unsupported));
            }

            return Platform::removeEmptyDirectory(path, options);
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(GameWIP::Status::Types::Status(ErrorCode::Unknown));
        }
    }

    GameWIP::Status::Types::Result<std::uint64_t>
    removeDirectoryTree(const Types::Path &path, const Types::Directory::RemoveTreeOptions &options) noexcept
    {
        try
        {
            // Validate the request and resolve the initial entry.
            if(path.empty() || !isValidSymlinkPolicy(options.symlinkPolicy))
            {
                return removeTreeFailure(ErrorCode::InvalidArgument);
            }

            auto existing = queryEntry(path, options.symlinkPolicy);

            if(!existing.ok())
            {
                if(existing.status().code() == ErrorCode::NotFound && options.succeedIfMissing)
                {
                    return GameWIP::Status::Types::Result<std::uint64_t>::success(0);
                }
                return removeTreeFailure(existing.status());
            }

            if(existing.value().kind != Types::EntryKind::Directory)
            {
                return removeTreeFailure(ErrorCode::NotDirectory);
            }

            if(options.symlinkPolicy != Types::SymlinkPolicy::DoNotFollow && finalEntryIsSymlink(path))
            {
                return removeTreeFailure(ErrorCode::Unsupported);
            }

            const auto rootPath = Platform::isRootPath(path);

            if(!rootPath.ok())
            {
                return removeTreeFailure(rootPath.status());
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
                std::unique_ptr<Internal::DirectoryCursorState> cursor;
            };

            // Keep only active ancestry so retained state grows with depth, not total entry count.
            std::vector<RemovalFrame> pending;
            pending.push_back(RemovalFrame{.path = path, .info = existing.value()});

            std::uint64_t removedEntries = 0;

            while(!pending.empty())
            {
                // Enforce the limit before removing the next entry.
                if(removedEntries >= options.maxEntries && !(rootPath.value() && pending.size() == 1))
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
                        GameWIP::Status::Types::Result<std::unique_ptr<Internal::DirectoryCursorState>> opened =
                            pending.size() == 1
                                ? Platform::openDirectoryCursor(current.path, Types::SymlinkPolicy::DoNotFollow)
                                : Platform::openChildDirectoryCursor(
                                      *pending[pending.size() - 2].cursor,
                                      current.path,
                                      Types::SymlinkPolicy::DoNotFollow);

                        if(!opened.ok())
                        {
                            return removeTreeFailure(opened.status(), removedEntries);
                        }

                        current.cursor = std::move(opened).value();
                    }

                    auto child = Platform::readDirectoryCursor(*current.cursor);

                    if(!child.ok())
                    {
                        return removeTreeFailure(child.status(), removedEntries);
                    }

                    if(child.value())
                    {
                        Types::Directory::Entry &entry = child.value()->entry;
                        pending.push_back(RemovalFrame{.path = std::move(entry.path), .info = entry.info});
                        continue;
                    }

                    current.cursor.reset();
                }

                // Filesystem roots contain removable entries but cannot be removed themselves.
                if(rootPath.value() && pending.size() == 1)
                {
                    pending.pop_back();
                    continue;
                }

                // Remove the leaf or the now-empty directory.
                auto removeResult =
                    current.info.kind == Types::EntryKind::Directory
                        ? removeEmptyDirectory(
                              current.path,
                              Types::RemoveOptions{.succeedIfMissing = false, .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow})
                        : removeFile(
                              current.path,
                              Types::RemoveOptions{.succeedIfMissing = false, .symlinkPolicy = Types::SymlinkPolicy::DoNotFollow});

                if(!removeResult.ok())
                {
                    return removeTreeFailure(removeResult.status(), removedEntries);
                }

                // Record progress only after removal succeeds.
                pending.pop_back();
                ++removedEntries;
            }

            return GameWIP::Status::Types::Result<std::uint64_t>::success(removedEntries);
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
