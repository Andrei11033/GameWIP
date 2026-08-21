/// @file types.h
/// @brief Shared passive vocabulary for the TestSupport library.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace GameWIP::TestSupport
{
    /// @brief Passive TestSupport status and result values shared across feature surfaces.
    namespace Types
    {
        /// @brief Portable error categories for TestSupport-owned infrastructure operations.
        /// @note Enumerator numeric values are not serialization identifiers or stable wire-format values.
        enum class InfrastructureError : std::uint8_t
        {
            None,                    ///< The infrastructure operation completed successfully.
            InvalidArgument,         ///< An argument is invalid after control entered the operation.
            Unsupported,             ///< No supported backend implements the requested operation.
            OutOfMemory,             ///< TestSupport could not allocate implementation-owned storage.
            EncodingFailed,          ///< Data designated as text is malformed or incomplete UTF-8.
            ProcessSetupFailed,      ///< Child-process native setup failed before normal execution.
            ProcessLaunchFailed,     ///< The child executable could not be launched.
            ProcessCleanupFailed,    ///< Timeout or failure cleanup could not be completed as requested.
            PipeCreationFailed,      ///< Child-output pipe creation or configuration failed.
            CaptureFailed,           ///< Child-output capture setup, reading, or worker execution failed.
            WaitFailed,              ///< Waiting for a child process failed.
            ProcessInspectionFailed, ///< A started child could not be inspected for its exit state.
            EnvironmentFailed,       ///< A process-environment read or mutation failed.
            FileOperationFailed,     ///< A filesystem or file operation failed.
            PlatformFailure          ///< A platform operation failed without a more specific stable category.
        };

        /// @brief Compact status returned by expected TestSupport infrastructure operations.
        struct InfrastructureStatus
        {
            InfrastructureError error = InfrastructureError::None; ///< Stable TestSupport-owned error category.
            std::uint64_t nativeCode = 0;                          ///< Platform/native diagnostic code, or zero when unavailable.

            /// @brief Returns true only when the infrastructure operation completed successfully.
            [[nodiscard]] constexpr bool ok() const noexcept
            {
                return error == InfrastructureError::None;
            }
        };

        /// @brief Result returned by a UTF-8 text-producing infrastructure operation.
        struct TextResult
        {
            InfrastructureStatus status; ///< Infrastructure operation status.
            std::string text;            ///< Valid UTF-8 text; failed reads may retain a complete valid prefix.
        };

        /// @brief Result returned by a boolean infrastructure query.
        struct BoolResult
        {
            InfrastructureStatus status; ///< Infrastructure operation status.
            bool value = false;          ///< Domain value; interpret only when the owning operation documents it as meaningful.
        };

        /// @brief Result returned by a counting infrastructure query.
        struct CountResult
        {
            InfrastructureStatus status; ///< Infrastructure operation status.
            std::size_t count = 0;       ///< Domain count; zero remains a valid successful value.
        };
    } // namespace Types

    /// @brief Formats a TestSupport infrastructure status for human-readable reporting.
    /// @param status Status to describe.
    /// @return Stable error-category name plus the numeric native diagnostic when present.
    /// @note Formatting is performed only when requested and may allocate.
    [[nodiscard]] std::string formatInfrastructureStatus(const Types::InfrastructureStatus &status);
} // namespace GameWIP::TestSupport
