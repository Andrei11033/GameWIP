/// @file status.h
/// @brief Public status vocabulary and helpers for GameWIP IO.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/// @brief Platform-neutral byte-transfer contracts shared by low-level GameWIP libraries.
/// @details IO defines portable status/result shapes and resource-agnostic transfer interfaces. It
/// does not open operating-system resources or provide a platform backend.
namespace GameWIP::IO
{

    /// @brief IO status, result, and option types.
    namespace Types
    {
        /// @brief Portable error categories shared by IO and resource-owning backend libraries.
        /// @note Enumerator numeric values are not serialization IDs or stable wire-format values.
        enum class ErrorCode
        {
            /// @brief Operation completed successfully.
            Success,

            /// @brief An argument violates the operation contract.
            InvalidArgument,
            /// @brief The requested operation or capability is not supported.
            Unsupported,
            /// @brief The resource or memory helper is not open.
            NotOpen,
            /// @brief The resource is already open.
            AlreadyOpen,

            /// @brief The requested resource does not exist.
            NotFound,
            /// @brief The requested resource already exists.
            AlreadyExists,
            /// @brief The caller lacks permission for the requested operation.
            PermissionDenied,
            /// @brief A path exceeds a backend-supported limit.
            PathTooLong,

            /// @brief The requested resource is a directory when a non-directory was required.
            IsDirectory,
            /// @brief The requested resource is not a directory.
            NotDirectory,
            /// @brief The stream does not support seeking or a related position capability.
            NotSeekable,
            /// @brief The stream has reached end-of-stream where an explicit status is required.
            EndOfStream,

            /// @brief Opening a resource failed without a more specific portable code.
            OpenFailed,
            /// @brief Reading failed without a more specific portable code.
            ReadFailed,
            /// @brief Writing failed without a more specific portable code.
            WriteFailed,
            /// @brief Flushing failed without a more specific portable code.
            FlushFailed,
            /// @brief Closing failed without a more specific portable code.
            CloseFailed,
            /// @brief Seeking failed without a more specific portable code.
            SeekFailed,
            /// @brief Querying resource metadata failed.
            StatFailed,
            /// @brief Removing a resource failed.
            RemoveFailed,
            /// @brief Replacing a resource failed.
            ReplaceFailed,
            /// @brief Copying a resource failed.
            CopyFailed,
            /// @brief Moving or renaming a resource failed.
            MoveFailed,
            /// @brief Resizing a resource failed.
            ResizeFailed,
            /// @brief Acquiring a resource lock failed.
            LockFailed,
            /// @brief Releasing a resource lock failed.
            UnlockFailed,
            /// @brief Creating a directory failed.
            DirectoryCreateFailed,
            /// @brief Listing a directory failed.
            DirectoryListFailed,
            /// @brief A directory could not be removed because it is not empty.
            DirectoryNotEmpty,

            /// @brief A read ended before the promised byte count was produced.
            PartialRead,
            /// @brief A write accepted fewer bytes than required before it failed.
            PartialWrite,
            /// @brief A requested, known, or observed size exceeds the accepted limit.
            SizeLimitExceeded,
            /// @brief A required memory allocation failed.
            OutOfMemory,

            /// @brief The resource is busy or has an incompatible lock/share state.
            ResourceBusy,
            /// @brief Storage or quota capacity is exhausted.
            StorageFull,
            /// @brief A pipe or redirected stream no longer has a reader.
            BrokenPipe,
            /// @brief The operation was interrupted before completion.
            Interrupted,

            /// @brief Text encoding or conversion failed.
            EncodingFailed,
            /// @brief A backend-native failure has no more specific portable code.
            NativeFailure,
            /// @brief The failure category is unknown.
            Unknown
        };

        /// @brief Status returned by expected IO operations.
        struct Status
        {
            /// @brief Portable error category for the operation.
            ErrorCode code = ErrorCode::Success;
            /// @brief Backend-native error code when a concrete backend has one, otherwise zero.
            std::int64_t nativeCode = 0;
            /// @brief Developer-facing UTF-8 diagnostic text; not stable for machine parsing.
            std::string message;

            /// @brief Returns true when the operation succeeded.
            /// @return True for ErrorCode::Success.
            [[nodiscard]] constexpr bool ok() const noexcept
            {
                return code == ErrorCode::Success;
            }
        };
    } // namespace Types

    /// @brief Creates an IO status with portable, native, and diagnostic details.
    /// @param code Portable status code used for program decisions.
    /// @param nativeCode Backend-native error code, or zero when unavailable.
    /// @param message Developer-facing UTF-8 diagnostic text; not stable for machine parsing.
    /// @return Status containing the supplied values.
    /// @pre message is valid UTF-8 when non-empty.
    /// @note The function is non-throwing, but constructing the by-value message argument occurs
    /// before function entry and may allocate. Code-only calls avoid that allocation.
    [[nodiscard]] Types::Status makeStatus(Types::ErrorCode code, std::int64_t nativeCode = 0, std::string message = {}) noexcept;

    /// @brief Creates a successful IO status.
    /// @return Status whose code is ErrorCode::Success.
    [[nodiscard]] Types::Status successStatus() noexcept;

    /// @brief Returns the symbolic name of an ErrorCode value.
    /// @param code Error code to name.
    /// @return Stable non-owning string literal for known values, or "Unknown" for unknown enumerators.
    /// @note Use the name for diagnostics and tests, not as a substitute for an application-owned
    /// serialized or wire-format error representation.
    [[nodiscard]] std::string_view errorCodeName(Types::ErrorCode code) noexcept;
} // namespace GameWIP::IO
