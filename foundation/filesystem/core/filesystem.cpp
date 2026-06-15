#include "filesystem/filesystem.h"
#include "filesystem/internal/filesystem_platform.h"

#include <system_error>
#include <utility>

namespace GameWIP::FileSystem
{

    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        Types::PathResult pathFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .path = Types::Path{}};
        }

        Types::PathResult pathFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .path = Types::Path{}};
        }

        Types::BoolResult boolFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .value = false};
        }

        Types::BoolResult boolFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .value = false};
        }

        Types::Utf8PathResult utf8Failure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .utf8 = std::string{}};
        }

        IO::Types::Status statusFromStdError(std::error_code ec, ErrorCode fallback)
        {
            if (!ec)
            {
                return IO::successStatus();
            }

            ErrorCode code = fallback;

            if (ec == std::errc::no_such_file_or_directory)
            {
                code = ErrorCode::NotFound;
            }
            else if (ec == std::errc::not_a_directory)
            {
                code = ErrorCode::NotDirectory;
            }
            else if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted)
            {
                code = ErrorCode::PermissionDenied;
            }
            else if (ec == std::errc::file_exists)
            {
                code = ErrorCode::AlreadyExists;
            }
            else if (ec == std::errc::filename_too_long)
            {
                code = ErrorCode::PathTooLong;
            }
            else if (ec == std::errc::no_space_on_device)
            {
                code = ErrorCode::StorageFull;
            }
            else if (ec == std::errc::device_or_resource_busy)
            {
                code = ErrorCode::ResourceBusy;
            }
            else if (ec == std::errc::interrupted)
            {
                code = ErrorCode::Interrupted;
            }
            else if (ec == std::errc::too_many_symbolic_link_levels)
            {
                code = ErrorCode::Unsupported;
            }
            else if (ec == std::errc::directory_not_empty)
            {
                code = ErrorCode::DirectoryNotEmpty;
            }

            return IO::makeStatus(code, ec.value(), ec.message());
        }

        Detail::Platform::EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy)
        {
            if (path.empty())
            {
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }

            switch (symlinkPolicy)
            {
            case Types::SymlinkPolicy::DoNotFollow:
            case Types::SymlinkPolicy::FollowFinal:
            case Types::SymlinkPolicy::FollowAll:
                return Detail::Platform::queryEntry(path, symlinkPolicy);
            default:
                return {.status = IO::makeStatus(ErrorCode::InvalidArgument)};
            }
        }

    } // namespace

    Types::BoolResult exists(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = true};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isRegularFile(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::RegularFile};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isDirectory(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::Directory};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::BoolResult isSymlink(const Types::Path &path, const Types::QueryOptions &options) noexcept
    {
        try
        {
            const Detail::Platform::EntryQueryResult result = queryEntry(path, options.symlinkPolicy);
            if (result.status.code == ErrorCode::NotFound)
            {
                return {.status = IO::successStatus(), .value = false};
            }
            if (!result.status.ok())
            {
                return boolFailure(result.status);
            }
            return {.status = IO::successStatus(), .value = result.info.kind == Types::EntryKind::Symlink};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult getCurrentDirectory() noexcept
    {
        try
        {
            std::error_code ec;
            Types::Path path = std::filesystem::current_path(ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = path};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    IO::Types::Status setCurrentDirectory(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            std::filesystem::current_path(path, ec);
            return statusFromStdError(ec, ErrorCode::OpenFailed);
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(ErrorCode::Unknown);
        }
    }

    Types::PathResult parentPath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.parent_path()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult filename(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.filename()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult stem(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.stem()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult extension(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .path = path.extension()};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult replaceExtension(const Types::Path &path, const Types::Path &newExtension) noexcept
    {
        try
        {
            Types::Path result = path;
            result.replace_extension(newExtension);

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult joinPath(const Types::Path &left, const Types::Path &right) noexcept
    {
        try
        {
            Types::Path result = left;
            result /= right;

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::BoolResult isAbsolutePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .value = path.is_absolute()};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::BoolResult isRelativePath(const Types::Path &path) noexcept
    {
        try
        {
            return {.status = IO::successStatus(), .value = path.is_relative()};
        }
        catch (const std::bad_alloc &)
        {
            return boolFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return boolFailure(ErrorCode::InvalidArgument);
        }
    }

    Types::PathResult absolutePath(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::absolute(path, ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult canonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::canonical(path, ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult weaklyCanonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            if (path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult getTemporaryDirectoryPath() noexcept
    {
        try
        {
            std::error_code ec;
            Types::Path result = std::filesystem::temp_directory_path(ec);
            if (ec)
            {
                return pathFailure(statusFromStdError(ec, ErrorCode::StatFailed));
            }
            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    Types::PathResult pathFromUtf8(std::string_view utf8Path) noexcept
    {
        try
        {
            const char8_t *u8Data = reinterpret_cast<const char8_t *>(utf8Path.data());
            std::u8string_view u8View{u8Data, utf8Path.size()};
            Types::Path result{u8View};

            return {.status = IO::successStatus(), .path = result};
        }
        catch (const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return pathFailure(ErrorCode::EncodingFailed);
        }
    }

    Types::Utf8PathResult pathToUtf8(const Types::Path &path) noexcept
    {
        try
        {
            std::u8string u8String = path.u8string();
            std::string utf8{u8String.begin(), u8String.end()};

            return {.status = IO::successStatus(), .utf8 = utf8};
        }
        catch (const std::bad_alloc &)
        {
            return utf8Failure(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return utf8Failure(ErrorCode::EncodingFailed);
        }
    }
} // namespace GameWIP::FileSystem
