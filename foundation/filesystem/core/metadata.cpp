/// @file metadata.cpp
/// @brief FileSystem entry predicates and metadata queries.

#include "filesystem/internal/helpers.h"
#include "filesystem/internal/filesystem_platform.h"

#include <new>
#include <utility>

namespace GameWIP::FileSystem
{
    using namespace Detail;

    namespace
    {
        // Predicate and metadata failure payloads

        Types::EntryInfoResult entryInfoFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .info = Types::EntryInfo{}};
        }

        Types::EntryInfoResult entryInfoFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .info = Types::EntryInfo{}};
        }

        Types::LastWriteTimeResult lastWriteTimeFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .time = Types::FileTime{}};
        }

        Types::LastWriteTimeResult lastWriteTimeFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .time = Types::FileTime{}};
        }

        // Shared entry-predicate flow

        template <typename Predicate>
        Types::BoolResult queryEntryPredicate(const Types::Path &path, const Types::EntryOptions &options, Predicate &&matches) noexcept
        {
            try
            {
                Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);

                if(result.status.code == ErrorCode::NotFound)
                {
                    return {.status = IO::successStatus(), .value = false};
                }

                if(!result.status.ok())
                {
                    return boolFailure(std::move(result.status));
                }

                return {.status = IO::successStatus(), .value = matches(result.info)};
            }
            catch(const std::bad_alloc &)
            {
                return Detail::boolFailure(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return Detail::boolFailure(ErrorCode::Unknown);
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Entry presence and metadata
    /// ------------------------------------------------------------

    Types::BoolResult exists(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &) noexcept
            {
                return true;
            });
    }

    Types::EntryInfoResult getEntryInfo(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        try
        {
            Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);

            if(!result.status.ok())
            {
                return entryInfoFailure(std::move(result.status));
            }

            return {.status = IO::successStatus(), .info = result.info};
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

    Types::BoolResult isRegularFile(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &info) noexcept
            {
                return info.kind == Types::EntryKind::RegularFile;
            });
    }

    Types::BoolResult isDirectory(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        return queryEntryPredicate(
            path,
            options,
            [](const Types::EntryInfo &info) noexcept
            {
                return info.kind == Types::EntryKind::Directory;
            });
    }

    Types::BoolResult isSymlink(const Types::Path &path, const Types::EntryOptions &options) noexcept
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

    IO::Types::SizeResult getFileSize(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        Types::EntryInfoResult info = getEntryInfo(path, options);

        if(!info.status.ok())
        {
            return sizeFailure(std::move(info.status));
        }

        if(info.info.kind != Types::EntryKind::RegularFile || !info.info.hasSize)
        {
            return sizeFailure(ErrorCode::InvalidArgument);
        }

        return {.status = IO::successStatus(), .sizeBytes = info.info.sizeBytes};
    }

    Types::LastWriteTimeResult getLastWriteTime(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        Types::EntryInfoResult info = getEntryInfo(path, options);

        if(!info.status.ok())
        {
            return lastWriteTimeFailure(std::move(info.status));
        }

        if(!info.info.hasLastWriteTime)
        {
            return lastWriteTimeFailure(ErrorCode::StatFailed);
        }

        return {.status = IO::successStatus(), .time = info.info.lastWriteTime};
    }

    Types::BoolResult isReadOnly(const Types::Path &path, const Types::EntryOptions &options) noexcept
    {
        Types::EntryInfoResult info = getEntryInfo(path, options);

        if(!info.status.ok())
        {
            return boolFailure(std::move(info.status));
        }

        return {.status = IO::successStatus(), .value = info.info.readOnly};
    }
} // namespace GameWIP::FileSystem
