/// @file status.h
/// @brief Portable operation status and diagnostic details.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/// @brief Platform-neutral status codes and optional operation diagnostics.
namespace GameWIP::Status
{
    /// @brief Passive status and diagnostic types.
    namespace Types
    {
        /// @brief Portable outcomes shared by fallible GameWIP operations.
        /// @note Numeric values are not serialization IDs or stable wire-format values.
        enum class Code : std::uint8_t
        {
            // Operation state
            Success,         ///< Operation completed successfully.
            InvalidArgument, ///< An argument violates the operation contract.
            Unsupported,     ///< The operation or requested capability is unavailable.
            NotOpen,         ///< The resource or session is not open.
            AlreadyOpen,     ///< The resource or session is already open.

            // Resource lookup and type
            NotFound,         ///< The requested resource does not exist.
            AlreadyExists,    ///< The requested resource already exists.
            PermissionDenied, ///< The operation is not permitted.
            PathTooLong,      ///< The path exceeds a supported length.
            IsDirectory,      ///< A file-like resource was required but a directory was found.
            NotDirectory,     ///< A directory was required but another resource was found.
            NotSeekable,      ///< The resource does not support seeking.
            EndOfStream,      ///< The stream has no more data.

            // Resource operations
            OpenFailed,            ///< Opening a resource failed.
            ReadFailed,            ///< Reading a resource failed.
            WriteFailed,           ///< Writing a resource failed.
            FlushFailed,           ///< Flushing a resource failed.
            CloseFailed,           ///< Closing a resource failed.
            SeekFailed,            ///< Changing the resource position failed.
            StatFailed,            ///< Querying resource metadata failed.
            RemoveFailed,          ///< Removing a resource failed.
            ReplaceFailed,         ///< Replacing a resource failed.
            CopyFailed,            ///< Copying a resource failed.
            MoveFailed,            ///< Moving or renaming a resource failed.
            ResizeFailed,          ///< Resizing a resource failed.
            LockFailed,            ///< Acquiring a resource lock failed.
            UnlockFailed,          ///< Releasing a resource lock failed.
            DirectoryCreateFailed, ///< Creating a directory failed.
            DirectoryListFailed,   ///< Listing a directory failed.
            DirectoryNotEmpty,     ///< The directory is not empty.

            // Progress and limits
            PartialRead,         ///< A read returned fewer bytes than requested before stopping.
            PartialWrite,        ///< A write accepted fewer bytes than required before stopping.
            SizeLimitExceeded,   ///< A requested or observed size exceeds an accepted limit.
            DestinationTooSmall, ///< The destination cannot hold the requested output.

            // Availability and control flow
            OutOfMemory,  ///< A required allocation failed.
            ResourceBusy, ///< The resource is busy or has an incompatible ownership state.
            StorageFull,  ///< Available storage or quota is exhausted.
            BrokenPipe,   ///< A pipe or redirected stream has no reader.
            Interrupted,  ///< The operation was interrupted before completion.
            WouldBlock,   ///< The operation cannot proceed without waiting.
            TimedOut,     ///< The operation exceeded its time limit.
            Cancelled,    ///< The operation was cancelled.

            // Encoding and algorithm inputs
            EncodingFailed,     ///< Encoding or conversion failed.
            InvalidEncoding,    ///< Input is malformed for the requested encoding.
            IncompleteEncoding, ///< Input ends with an incomplete encoded value.
            InvalidScalar,      ///< A value is not a valid Unicode scalar.
            OverlappingRanges,  ///< Input and output ranges overlap where disallowed.

            // Process and test infrastructure
            ProcessSetupFailed,      ///< Preparing process state failed.
            ProcessLaunchFailed,     ///< Starting a process failed.
            ProcessCleanupFailed,    ///< Cleaning up process state failed.
            PipeCreationFailed,      ///< Creating a process pipe failed.
            ProcessCaptureFailed,    ///< Capturing process output failed.
            ProcessWaitFailed,       ///< Waiting for a process failed.
            ProcessInspectionFailed, ///< Querying process state failed.
            EnvironmentFailed,       ///< Reading or updating environment state failed.
            ProcessStateConflict,    ///< Process state changed outside the operation.
            FileOperationFailed,     ///< A generic file operation failed.

            // Fallback
            NativeFailure, ///< A backend failed without a more specific portable code.
            Unknown        ///< The failure has no known portable category.
        };

        /// @brief Source family for a supplementary native error value.
        enum class NativeCodeDomain : std::uint8_t
        {
            Win32Error, ///< Win32 last-error value.
            HResult,    ///< HRESULT value.
            NtStatus,   ///< NTSTATUS value.
            Errno,      ///< C or POSIX errno value.
            Winsock,    ///< Winsock error value.
            Other       ///< Another backend-native code family.
        };

        /// @brief Native error value retained for diagnostics, never for portable decisions.
        struct NativeCode
        {
            NativeCodeDomain domain = NativeCodeDomain::Other; ///< Native code family.
            std::int64_t value = 0;                            ///< Native code value as reported by the backend.
        };

        /// @brief Optional detail attached to an operation status.
        struct Diagnostic
        {
            // Native error detail
            std::optional<NativeCode> nativeCode{}; ///< Native value and source family, when available.

            // Human-readable detail
            std::string message{}; ///< Developer-facing UTF-8 text; not stable for machine parsing.
        };

        /// @brief Portable operation outcome with shared, immutable optional diagnostics.
        /// @details Code-only statuses do not allocate. Diagnostic objects are shared when a status is copied. Message text must be UTF-8 and is not
        /// validated.
        class [[nodiscard]] Status
        {
        public:
            /// @brief Creates a successful status without allocating.
            Status() noexcept = default;

            /// @brief Creates a status from its portable outcome without allocating.
            explicit Status(Code code) noexcept
                : code_(code)
            {
            }

            /// @brief Returns the portable outcome used for program decisions.
            [[nodiscard]] Code code() const noexcept
            {
                return code_;
            }

            /// @brief Reports whether the outcome is Code::Success.
            [[nodiscard]] bool ok() const noexcept
            {
                return code_ == Code::Success;
            }

            /// @brief Returns optional immutable diagnostics, or null when none are present.
            [[nodiscard]] const Diagnostic *diagnostic() const noexcept
            {
                return diagnostic_.get();
            }

            /// @brief Creates a status with optional UTF-8 diagnostic text.
            /// @param code Portable outcome used for program decisions.
            /// @param message Developer-facing UTF-8 text, copied when non-empty.
            /// @details If diagnostic allocation fails, the portable code is retained and detail is omitted.
            [[nodiscard]] static Status withDiagnostic(Code code, std::string_view message) noexcept
            {
                Status result(code);
                result.diagnostic_ = createDiagnostic(std::nullopt, message);
                return result;
            }

            /// @brief Creates a status with a native code and optional UTF-8 diagnostic text.
            /// @param code Portable outcome used for program decisions.
            /// @param nativeCode Native value and source family, for diagnostics only.
            /// @param message Developer-facing UTF-8 text, copied when non-empty.
            /// @details If diagnostic allocation fails, the portable code is retained and detail is omitted.
            [[nodiscard]] static Status withDiagnostic(Code code, NativeCode nativeCode, std::string_view message = {}) noexcept
            {
                Status result(code);
                result.diagnostic_ = createDiagnostic(nativeCode, message);
                return result;
            }

        private:
            // Portable outcome and optional detail
            Code code_ = Code::Success;
            std::shared_ptr<const Diagnostic> diagnostic_{};

            /// @brief Builds optional detail without replacing the portable outcome on allocation failure.
            [[nodiscard]] static std::shared_ptr<const Diagnostic> createDiagnostic(
                std::optional<NativeCode> nativeCode,
                std::string_view message) noexcept
            {
                if(!nativeCode.has_value() && message.empty())
                {
                    return {};
                }

                try
                {
                    auto diagnostic = std::make_shared<Diagnostic>();
                    diagnostic->nativeCode = nativeCode;
                    diagnostic->message.assign(message);
                    return diagnostic;
                }
                catch(...)
                {
                    // Diagnostic detail is optional; retain the portable code.
                    return {};
                }
            }
        };

        /// @brief Returns the symbolic name of a status code.
        [[nodiscard]] constexpr std::string_view codeName(Code code) noexcept
        {
            switch(code)
            {
            case Code::Success:
                return "Success";
            case Code::InvalidArgument:
                return "InvalidArgument";
            case Code::Unsupported:
                return "Unsupported";
            case Code::NotOpen:
                return "NotOpen";
            case Code::AlreadyOpen:
                return "AlreadyOpen";
            case Code::NotFound:
                return "NotFound";
            case Code::AlreadyExists:
                return "AlreadyExists";
            case Code::PermissionDenied:
                return "PermissionDenied";
            case Code::PathTooLong:
                return "PathTooLong";
            case Code::IsDirectory:
                return "IsDirectory";
            case Code::NotDirectory:
                return "NotDirectory";
            case Code::NotSeekable:
                return "NotSeekable";
            case Code::EndOfStream:
                return "EndOfStream";
            case Code::OpenFailed:
                return "OpenFailed";
            case Code::ReadFailed:
                return "ReadFailed";
            case Code::WriteFailed:
                return "WriteFailed";
            case Code::FlushFailed:
                return "FlushFailed";
            case Code::CloseFailed:
                return "CloseFailed";
            case Code::SeekFailed:
                return "SeekFailed";
            case Code::StatFailed:
                return "StatFailed";
            case Code::RemoveFailed:
                return "RemoveFailed";
            case Code::ReplaceFailed:
                return "ReplaceFailed";
            case Code::CopyFailed:
                return "CopyFailed";
            case Code::MoveFailed:
                return "MoveFailed";
            case Code::ResizeFailed:
                return "ResizeFailed";
            case Code::LockFailed:
                return "LockFailed";
            case Code::UnlockFailed:
                return "UnlockFailed";
            case Code::DirectoryCreateFailed:
                return "DirectoryCreateFailed";
            case Code::DirectoryListFailed:
                return "DirectoryListFailed";
            case Code::DirectoryNotEmpty:
                return "DirectoryNotEmpty";
            case Code::PartialRead:
                return "PartialRead";
            case Code::PartialWrite:
                return "PartialWrite";
            case Code::SizeLimitExceeded:
                return "SizeLimitExceeded";
            case Code::DestinationTooSmall:
                return "DestinationTooSmall";
            case Code::OutOfMemory:
                return "OutOfMemory";
            case Code::ResourceBusy:
                return "ResourceBusy";
            case Code::StorageFull:
                return "StorageFull";
            case Code::BrokenPipe:
                return "BrokenPipe";
            case Code::Interrupted:
                return "Interrupted";
            case Code::WouldBlock:
                return "WouldBlock";
            case Code::TimedOut:
                return "TimedOut";
            case Code::Cancelled:
                return "Cancelled";
            case Code::EncodingFailed:
                return "EncodingFailed";
            case Code::InvalidEncoding:
                return "InvalidEncoding";
            case Code::IncompleteEncoding:
                return "IncompleteEncoding";
            case Code::InvalidScalar:
                return "InvalidScalar";
            case Code::OverlappingRanges:
                return "OverlappingRanges";
            case Code::ProcessSetupFailed:
                return "ProcessSetupFailed";
            case Code::ProcessLaunchFailed:
                return "ProcessLaunchFailed";
            case Code::ProcessCleanupFailed:
                return "ProcessCleanupFailed";
            case Code::PipeCreationFailed:
                return "PipeCreationFailed";
            case Code::ProcessCaptureFailed:
                return "ProcessCaptureFailed";
            case Code::ProcessWaitFailed:
                return "ProcessWaitFailed";
            case Code::ProcessInspectionFailed:
                return "ProcessInspectionFailed";
            case Code::EnvironmentFailed:
                return "EnvironmentFailed";
            case Code::ProcessStateConflict:
                return "ProcessStateConflict";
            case Code::FileOperationFailed:
                return "FileOperationFailed";
            case Code::NativeFailure:
                return "NativeFailure";
            case Code::Unknown:
                return "Unknown";
            }

            return "Unknown";
        }

        /// @brief Returns the symbolic name of a native-code source family.
        [[nodiscard]] constexpr std::string_view nativeCodeDomainName(NativeCodeDomain domain) noexcept
        {
            switch(domain)
            {
            case NativeCodeDomain::Win32Error:
                return "Win32Error";
            case NativeCodeDomain::HResult:
                return "HResult";
            case NativeCodeDomain::NtStatus:
                return "NtStatus";
            case NativeCodeDomain::Errno:
                return "Errno";
            case NativeCodeDomain::Winsock:
                return "Winsock";
            case NativeCodeDomain::Other:
                return "Other";
            }

            return "Other";
        }
    } // namespace Types

} // namespace GameWIP::Status
