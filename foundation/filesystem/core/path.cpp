/// @file path.cpp
/// @brief FileSystem path conversion, lexical helpers, and process-directory operations.

#include "filesystem/path.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <filesystem>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace GameWIP::FileSystem
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

        [[nodiscard]] Types::PathResult pathFailure(IO::Types::Status status) noexcept
        {
            return {.status = std::move(status), .path = Types::Path{}};
        }

        [[nodiscard]] Types::PathResult pathFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .path = Types::Path{}};
        }

        [[nodiscard]] Types::BoolResult boolFailure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .value = false};
        }

        [[nodiscard]] Types::Utf8PathResult utf8Failure(ErrorCode code) noexcept
        {
            return {.status = IO::makeStatus(code), .utf8 = std::string{}};
        }

        [[nodiscard]] bool isValidUtf8(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
        }

        [[nodiscard]] IO::Types::Status statusFromStdError(std::error_code ec, ErrorCode fallback) noexcept
        {
            if (!ec)
            {
                return IO::successStatus();
            }

            ErrorCode code = fallback;
            if (ec == std::errc::no_such_file_or_directory)
                code = ErrorCode::NotFound;
            else if (ec == std::errc::not_a_directory)
                code = ErrorCode::NotDirectory;
            else if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted)
                code = ErrorCode::PermissionDenied;
            else if (ec == std::errc::file_exists)
                code = ErrorCode::AlreadyExists;
            else if (ec == std::errc::filename_too_long)
                code = ErrorCode::PathTooLong;
            else if (ec == std::errc::no_space_on_device)
                code = ErrorCode::StorageFull;
            else if (ec == std::errc::device_or_resource_busy)
                code = ErrorCode::ResourceBusy;
            else if (ec == std::errc::interrupted)
                code = ErrorCode::Interrupted;
            else if (ec == std::errc::too_many_symbolic_link_levels)
                code = ErrorCode::Unsupported;
            else if (ec == std::errc::directory_not_empty)
                code = ErrorCode::DirectoryNotEmpty;

            try
            {
                std::string message = ec.message();
                if (isValidUtf8(message))
                    return IO::makeStatus(code, ec.value(), std::move(message));
                return IO::makeStatus(code, ec.value());
            }
            catch (...)
            {
                return IO::makeStatus(code, ec.value());
            }
        }

        [[nodiscard]] Types::Path pathFromTrustedUtf8(std::string_view utf8Path)
        {
            std::u8string converted(utf8Path.size(), u8'\0');
            if (!utf8Path.empty())
            {
                std::ranges::transform(
                    utf8Path,
                    converted.begin(),
                    [](char byte)
                    {
                        return static_cast<char8_t>(byte);
                    });
            }
            return Types::Path{converted};
        }
    } // namespace

    // ------------------------------------------------------------
    // Current directory
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Path components
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Path resolution
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Native path conversion
    // ------------------------------------------------------------

    Types::PathResult pathFromUtf8(std::string_view utf8Path) noexcept
    {
        if (!isValidUtf8(utf8Path))
        {
            return pathFailure(ErrorCode::EncodingFailed);
        }

        try
        {
            Types::Path result = pathFromTrustedUtf8(utf8Path);
            return {.status = IO::successStatus(), .path = std::move(result)};
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
            if (!isValidUtf8(utf8))
            {
                return utf8Failure(ErrorCode::EncodingFailed);
            }

            return {.status = IO::successStatus(), .utf8 = std::move(utf8)};
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
