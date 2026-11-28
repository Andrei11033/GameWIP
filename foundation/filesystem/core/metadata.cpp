/// @file metadata.cpp
/// @brief FileSystem entry predicates and metadata queries.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/platform.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Internal;

    namespace
    {
        // Predicate and metadata failure payloads

        GameWIP::Status::Types::Result<Types::EntryInfo> entryInfoFailure(GameWIP::Status::Types::Status status) noexcept
        {
            return GameWIP::Status::Types::Result<Types::EntryInfo>::failure(std::move(status));
        }

        GameWIP::Status::Types::Result<Types::EntryInfo> entryInfoFailure(ErrorCode code) noexcept
        {
            return entryInfoFailure(GameWIP::Status::Types::Status(code));
        }

        GameWIP::Status::Types::Result<Types::FileTime> lastWriteTimeFailure(GameWIP::Status::Types::Status status) noexcept
        {
            return GameWIP::Status::Types::Result<Types::FileTime>::failure(std::move(status));
        }

        GameWIP::Status::Types::Result<Types::FileTime> lastWriteTimeFailure(ErrorCode code) noexcept
        {
            return lastWriteTimeFailure(GameWIP::Status::Types::Status(code));
        }

        // Shared entry-predicate flow

        template <typename Predicate>
        GameWIP::Status::Types::Result<bool> queryEntryPredicate(
            const Types::Path &path,
            const Types::EntryOptions &options,
            Predicate &&matches) noexcept
        {
            try
            {
                auto result = queryEntry(path, options.symlinkPolicy);

                if(result.status().code() == ErrorCode::NotFound)
                {
                    return GameWIP::Status::Types::Result<bool>::success(false);
                }

                if(!result.ok())
                {
                    return boolFailure(result.status());
                }

                return GameWIP::Status::Types::Result<bool>::success(matches(result.value()));
            }
            catch(const std::bad_alloc &)
            {
                return Internal::boolFailure(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return Internal::boolFailure(ErrorCode::Unknown);
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Entry presence and metadata
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<bool> exists(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &) noexcept
            {
                return true;
            });
    }

    GameWIP::Status::Types::Result<Types::EntryInfo> getEntryInfo(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        try
        {
            return queryEntry(path, options.symlinkPolicy);
        }
        catch(const std::bad_alloc &)
        {
            return entryInfoFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return entryInfoFailure(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Entry-kind predicates
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<bool> isRegularFile(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &info) noexcept
            {
                return info.kind == Types::EntryKind::RegularFile;
            });
    }

    GameWIP::Status::Types::Result<bool> isDirectory(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &info) noexcept
            {
                return info.kind == Types::EntryKind::Directory;
            });
    }

    GameWIP::Status::Types::Result<bool> isSymlink(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &info) noexcept
            {
                return info.kind == Types::EntryKind::Symlink;
            });
    }

    /// ------------------------------------------------------------
    /// Metadata values
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<std::uint64_t> getFileSize(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        auto info = getEntryInfo(path, options);

        if(!info.ok())
        {
            return sizeFailure(info.status());
        }

        if(info.value().kind != Types::EntryKind::RegularFile || !info.value().hasSize)
        {
            return sizeFailure(ErrorCode::InvalidArgument);
        }

        return GameWIP::Status::Types::Result<std::uint64_t>::success(info.value().sizeBytes);
    }

    GameWIP::Status::Types::Result<Types::FileTime> getLastWriteTime(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        auto info = getEntryInfo(path, options);

        if(!info.ok())
        {
            return lastWriteTimeFailure(info.status());
        }

        if(!info.value().hasLastWriteTime)
        {
            return lastWriteTimeFailure(ErrorCode::StatFailed);
        }

        return GameWIP::Status::Types::Result<Types::FileTime>::success(info.value().lastWriteTime);
    }

    GameWIP::Status::Types::Result<bool> isReadOnly(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        auto info = getEntryInfo(path, options);

        if(!info.ok())
        {
            return boolFailure(info.status());
        }

        return GameWIP::Status::Types::Result<bool>::success(info.value().readOnly);
    }
} // namespace GameWIP::FileSystem
