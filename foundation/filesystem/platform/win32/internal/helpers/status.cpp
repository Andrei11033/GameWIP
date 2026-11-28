/// @file status.cpp
/// @brief Win32 FileSystem status and option helpers.

#include "filesystem/platform/win32/internal/helpers.h"
#include "unicode/unicode.h"

#include <string>
#include <system_error>
#include <utility>

namespace GameWIP::FileSystem::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Status and option helpers
    /// ------------------------------------------------------------

    /// @brief Maps common Win32 errors to portable status while preserving native detail.
    [[nodiscard]] Status makeWin32Status(DWORD error, ErrorCode fallback) noexcept
    {
        if(error == ERROR_SUCCESS)
        {
            return Status{};
        }

        ErrorCode code = fallback;

        switch(error)
        {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            code = ErrorCode::NotFound;
            break;
        case ERROR_DIRECTORY:
            code = ErrorCode::NotDirectory;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_PRIVILEGE_NOT_HELD:
            code = ErrorCode::PermissionDenied;
            break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            code = ErrorCode::AlreadyExists;
            break;
        case ERROR_FILENAME_EXCED_RANGE:
            code = ErrorCode::PathTooLong;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            code = ErrorCode::OutOfMemory;
            break;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            code = ErrorCode::ResourceBusy;
            break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:
            code = ErrorCode::StorageFull;
            break;
        case ERROR_DIR_NOT_EMPTY:
            code = ErrorCode::DirectoryNotEmpty;
            break;
        case ERROR_BAD_PATHNAME:
        case ERROR_INVALID_NAME:
        case ERROR_INVALID_PARAMETER:
            code = ErrorCode::InvalidArgument;
            break;
        default:
            break;
        }

        try
        {
            std::string message = std::system_category().message(static_cast<int>(error));

            if(Unicode::Utf8::validate(message).outcome == Unicode::Types::ValidationOutcome::Valid)
            {
                return Status::withDiagnostic(
                    code,
                    GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Win32Error, error},
                    message);
            }
            return Status::withDiagnostic(code, GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Win32Error, error});
        }
        catch(...)
        {
            return Status::withDiagnostic(code, GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Win32Error, error});
        }
    }

    /// @brief Maps the calling thread's current GetLastError value.
    [[nodiscard]] Status makeLastErrorStatus(ErrorCode fallback) noexcept
    {
        return makeWin32Status(GetLastError(), fallback);
    }

    /// @brief Builds a diagnostic status without allowing message allocation to escape.
    [[nodiscard]] Status makeDiagnosticStatus(ErrorCode code, std::int64_t nativeCode, std::string_view message) noexcept
    {
        try
        {
            if(Unicode::Utf8::validate(message).outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                return nativeCode == 0 ? Status(code)
                                       : Status::withDiagnostic(
                                             code,
                                             GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, nativeCode});
            }
            if(nativeCode == 0)
            {
                return Status::withDiagnostic(code, message);
            }
            return Status::withDiagnostic(
                code,
                GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, nativeCode},
                message);
        }
        catch(...)
        {
            return nativeCode == 0 ? Status(code)
                                   : Status::withDiagnostic(
                                         code,
                                         GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::Other, nativeCode});
        }
    }

    /// @brief Converts portable sharing flags to CreateFile/NtCreateFile flags.
    [[nodiscard]] DWORD nativeShareMode(Types::File::Share share) noexcept
    {
        DWORD result = 0;

        if((share & Types::File::Share::Read) == Types::File::Share::Read)
        {
            result |= FILE_SHARE_READ;
        }

        if((share & Types::File::Share::Write) == Types::File::Share::Write)
        {
            result |= FILE_SHARE_WRITE;
        }

        if((share & Types::File::Share::Delete) == Types::File::Share::Delete)
        {
            result |= FILE_SHARE_DELETE;
        }

        return result;
    }

    /// @brief Converts NTSTATUS through RtlNtStatusToDosError when available.
    [[nodiscard]] Status makeNtStatus(NTSTATUS status, ErrorCode fallback) noexcept
    {
        const NtApi &api = ntApi();

        if(api.ntStatusToDosError == nullptr)
        {
            return Status::withDiagnostic(
                fallback,
                GameWIP::Status::Types::NativeCode{GameWIP::Status::Types::NativeCodeDomain::NtStatus, status},
                "NTSTATUS conversion is unavailable");
        }

        return makeWin32Status(api.ntStatusToDosError(status), fallback);
    }

    /// @brief Returns the stable failure used when traversal encounters a forbidden symlink.
    [[nodiscard]] Status symlinkPolicyRejectedStatus() noexcept
    {
        return makeDiagnosticStatus(ErrorCode::PermissionDenied, 0, "path rejected by symlink policy");
    }

} // namespace GameWIP::FileSystem::Platform::Win32
