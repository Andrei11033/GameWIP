/// @file path.cpp
/// @brief FileSystem path conversion, lexical helpers, and process-directory operations.

#include "filesystem/path.h"
#include "filesystem/internal/helpers.h"

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
        using ErrorCode = GameWIP::Status::Types::Code;
        using OperationStatus = GameWIP::Status::Types::Status;

        [[nodiscard]] OperationStatus statusFromSystemError(std::error_code error, ErrorCode fallback) noexcept
        {
            if(!error)
            {
                return OperationStatus{};
            }

            ErrorCode code = fallback;

            if(error == std::errc::no_such_file_or_directory)
            {
                code = ErrorCode::NotFound;
            }
            else if(error == std::errc::not_a_directory)
            {
                code = ErrorCode::NotDirectory;
            }
            else if(error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
            {
                code = ErrorCode::PermissionDenied;
            }
            else if(error == std::errc::file_exists)
            {
                code = ErrorCode::AlreadyExists;
            }
            else if(error == std::errc::filename_too_long)
            {
                code = ErrorCode::PathTooLong;
            }
            else if(error == std::errc::no_space_on_device)
            {
                code = ErrorCode::StorageFull;
            }
            else if(error == std::errc::device_or_resource_busy)
            {
                code = ErrorCode::ResourceBusy;
            }
            else if(error == std::errc::interrupted)
            {
                code = ErrorCode::Interrupted;
            }
            else if(error == std::errc::too_many_symbolic_link_levels)
            {
                code = ErrorCode::Unsupported;
            }
            else if(error == std::errc::directory_not_empty)
            {
                code = ErrorCode::DirectoryNotEmpty;
            }

            try
            {
                std::string message = error.message();

                if(Internal::validUtf8(message))
                {
                    return OperationStatus::withDiagnostic(
                        code,
                        GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, error.value()},
                        message);
                }
                return OperationStatus::withDiagnostic(
                    code,
                    GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, error.value()});
            }
            catch(...)
            {
                return OperationStatus::withDiagnostic(
                    code,
                    GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, error.value()});
            }
        }

        [[nodiscard]] GameWIP::Status::Types::Result<Types::Path> pathFailure(OperationStatus status) noexcept
        {
            return GameWIP::Status::Types::Result<Types::Path>::failure(std::move(status));
        }

        [[nodiscard]] GameWIP::Status::Types::Result<Types::Path> pathFailure(ErrorCode code) noexcept
        {
            return pathFailure(OperationStatus(code));
        }

        template <typename Operation>
        [[nodiscard]] GameWIP::Status::Types::Result<Types::Path> pathResultFromOperation(Operation &&operation) noexcept
        {
            try
            {
                return GameWIP::Status::Types::Result<Types::Path>::success(operation());
            }
            catch(const std::bad_alloc &)
            {
                return pathFailure(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }
        }

        [[nodiscard]] GameWIP::Status::Types::Result<void> resultFromStatus(OperationStatus status) noexcept
        {
            if(status.ok())
            {
                return GameWIP::Status::Types::Result<void>::success();
            }

            return GameWIP::Status::Types::Result<void>::failure(std::move(status));
        }

    } // namespace

    /// ------------------------------------------------------------
    /// Current directory
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<Types::Path> getCurrentDirectory() noexcept
    {
        try
        {
            std::error_code ec;
            Types::Path path = std::filesystem::current_path(ec);

            if(ec)
            {
                return pathFailure(statusFromSystemError(ec, ErrorCode::StatFailed));
            }

            return GameWIP::Status::Types::Result<Types::Path>::success(std::move(path));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<void> setCurrentDirectory(const Types::Path &path) noexcept
    {
        try
        {
            if(path.empty())
            {
                return GameWIP::Status::Types::Result<void>::failure(OperationStatus(ErrorCode::InvalidArgument));
            }

            std::error_code ec;
            std::filesystem::current_path(path, ec);
            return resultFromStatus(statusFromSystemError(ec, ErrorCode::OpenFailed));
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(OperationStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(OperationStatus(ErrorCode::Unknown));
        }
    }

    /// ------------------------------------------------------------
    /// Path components
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<Types::Path> parentPath(const Types::Path &path) noexcept
    {
        return pathResultFromOperation(
            [&]
            {
                return path.parent_path();
            });
    }

    GameWIP::Status::Types::Result<Types::Path> filename(const Types::Path &path) noexcept
    {
        return pathResultFromOperation(
            [&]
            {
                return path.filename();
            });
    }

    GameWIP::Status::Types::Result<Types::Path> stem(const Types::Path &path) noexcept
    {
        return pathResultFromOperation(
            [&]
            {
                return path.stem();
            });
    }

    GameWIP::Status::Types::Result<Types::Path> extension(const Types::Path &path) noexcept
    {
        return pathResultFromOperation(
            [&]
            {
                return path.extension();
            });
    }

    GameWIP::Status::Types::Result<Types::Path> replaceExtension(const Types::Path &path, const Types::Path &newExtension) noexcept
    {
        return pathResultFromOperation(
            [&]
            {
                Types::Path result = path;
                result.replace_extension(newExtension);
                return result;
            });
    }

    GameWIP::Status::Types::Result<Types::Path> joinPath(const Types::Path &left, const Types::Path &right) noexcept
    {
        return pathResultFromOperation(
            [&]
            {
                Types::Path result = left;
                result /= right;
                return result;
            });
    }

    /// ------------------------------------------------------------
    /// Path resolution
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<bool> isAbsolutePath(const Types::Path &path) noexcept
    {
        try
        {
            return GameWIP::Status::Types::Result<bool>::success(path.is_absolute());
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<bool>::failure(OperationStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<bool>::failure(OperationStatus(ErrorCode::InvalidArgument));
        }
    }

    GameWIP::Status::Types::Result<bool> isRelativePath(const Types::Path &path) noexcept
    {
        try
        {
            return GameWIP::Status::Types::Result<bool>::success(path.is_relative());
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<bool>::failure(OperationStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<bool>::failure(OperationStatus(ErrorCode::InvalidArgument));
        }
    }

    GameWIP::Status::Types::Result<Types::Path> absolutePath(const Types::Path &path) noexcept
    {
        try
        {
            if(path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::absolute(path, ec);

            if(ec)
            {
                return pathFailure(statusFromSystemError(ec, ErrorCode::StatFailed));
            }

            return GameWIP::Status::Types::Result<Types::Path>::success(std::move(result));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<Types::Path> canonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            if(path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::canonical(path, ec);

            if(ec)
            {
                return pathFailure(statusFromSystemError(ec, ErrorCode::StatFailed));
            }

            return GameWIP::Status::Types::Result<Types::Path>::success(std::move(result));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<Types::Path> weaklyCanonicalPath(const Types::Path &path) noexcept
    {
        try
        {
            if(path.empty())
            {
                return pathFailure(ErrorCode::InvalidArgument);
            }

            std::error_code ec;
            Types::Path result = std::filesystem::weakly_canonical(path, ec);

            if(ec)
            {
                return pathFailure(statusFromSystemError(ec, ErrorCode::StatFailed));
            }

            return GameWIP::Status::Types::Result<Types::Path>::success(std::move(result));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    GameWIP::Status::Types::Result<Types::Path> getTemporaryDirectoryPath() noexcept
    {
        try
        {
            std::error_code ec;
            Types::Path result = std::filesystem::temp_directory_path(ec);

            if(ec)
            {
                return pathFailure(statusFromSystemError(ec, ErrorCode::StatFailed));
            }

            return GameWIP::Status::Types::Result<Types::Path>::success(std::move(result));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::Unknown);
        }
    }

    /// ------------------------------------------------------------
    /// Native path conversion
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<Types::Path> pathFromUtf8(std::string_view utf8Path) noexcept
    {
        if(!Internal::validUtf8(utf8Path))
        {
            return pathFailure(ErrorCode::EncodingFailed);
        }

        try
        {
            Types::Path result = Internal::pathFromTrustedUtf8(utf8Path);
            return GameWIP::Status::Types::Result<Types::Path>::success(std::move(result));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::EncodingFailed);
        }
    }

    GameWIP::Status::Types::Result<Types::Path> pathFromUtf8(std::u8string_view utf8Path) noexcept
    {
        try
        {
            std::string bytes(utf8Path.size(), '\0');
            std::ranges::transform(
                utf8Path,
                bytes.begin(),
                [](char8_t byte)
                {
                    return static_cast<char>(byte);
                });
            return pathFromUtf8(std::string_view(bytes));
        }
        catch(const std::bad_alloc &)
        {
            return pathFailure(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return pathFailure(ErrorCode::EncodingFailed);
        }
    }

    GameWIP::Status::Types::Result<std::string> pathToUtf8(const Types::Path &path) noexcept
    {
        try
        {
            std::u8string u8String = path.u8string();
            std::string utf8{u8String.begin(), u8String.end()};

            if(!Internal::validUtf8(utf8))
            {
                return GameWIP::Status::Types::Result<std::string>::failure(OperationStatus(ErrorCode::EncodingFailed));
            }

            return GameWIP::Status::Types::Result<std::string>::success(std::move(utf8));
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<std::string>::failure(OperationStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<std::string>::failure(OperationStatus(ErrorCode::EncodingFailed));
        }
    }
} // namespace GameWIP::FileSystem
