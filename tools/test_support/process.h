/// @file process.h
/// @brief Process-state guards and child-process execution helpers for TestSupport.

#pragma once

#include "test_support/types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::TestSupport
{
    /// @brief Default retained-memory limit for combined child stdout/stderr capture.
    /// @note Capture continues draining after this limit; discarded bytes are reported through `Result::outputTruncated`.
    inline constexpr std::size_t kDefaultMaxCapturedOutputBytes = std::size_t{4} * 1024 * 1024;

    /// @brief Passive child-process execution vocabulary.
    namespace Types::Process
    {
        /// @brief One child-environment override. A missing value unsets the variable for the child.
        struct EnvironmentOverride
        {
            std::string name;                 ///< UTF-8 environment variable name.
            std::optional<std::string> value; ///< UTF-8 value to set, or std::nullopt to unset the name.
        };

        /// @brief Runtime options for one child-process execution.
        struct Options
        {
            std::filesystem::path executablePath;                  ///< Executable path to launch.
            std::vector<std::string> arguments;                    ///< UTF-8 command-line arguments passed after the executable.
            std::vector<EnvironmentOverride> environmentOverrides; ///< Child environment overrides applied in vector order.
            std::chrono::milliseconds timeout{5000};               ///< Wait before TestSupport begins termination; negative waits indefinitely.
            bool captureOutput = true;                             ///< Routes stdout/stderr to one combined capture pipe when true.
            std::size_t maxCapturedOutputBytes = kDefaultMaxCapturedOutputBytes; ///< Retained capture byte limit; zero retains nothing.
            bool inheritParentEnvironment = true;                                ///< Copies the parent environment before overrides when true.
        };

        /// @brief Observable child-process outcome, separate from TestSupport infrastructure status.
        enum class Outcome : std::uint8_t
        {
            NotStarted,              ///< The child process was never created.
            Exited,                  ///< The child exited and Result::exitCode is exact.
            TimedOut,                ///< The configured wait expired and timeout enforcement succeeded.
            TerminatedDuringCleanup, ///< TestSupport requested termination while recovering from infrastructure failure.
            OutcomeUnavailable       ///< The child started but its final outcome could not be inspected.
        };

        /// @brief Result of one child-process execution.
        struct Result
        {
            InfrastructureStatus status;           ///< Infrastructure operation status, independent of child outcome.
            std::uint32_t exitCode = 0;            ///< Exact native exit code when outcome is Exited.
            Outcome outcome = Outcome::NotStarted; ///< Observable child-process outcome.
            bool outputTruncated = false;          ///< True when capture drained and discarded bytes beyond the retained limit.
            std::string outputBytes;               ///< Retained arbitrary combined stdout/stderr bytes; may contain malformed UTF-8.
        };
    } // namespace Types::Process

    /// @brief Temporarily sets a process environment variable and restores its prior state on destruction.
    class ScopedEnvironmentVariable
    {
    public:
        /// @brief Attempts to set name to value while retaining the previous process value.
        ScopedEnvironmentVariable(std::string_view name, std::string_view value) noexcept;
        /// @brief Best-effort restores the previous value or removes a previously missing name.
        ~ScopedEnvironmentVariable() noexcept;

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;            ///< Process-state ownership cannot be copied.
        ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete; ///< Process-state ownership cannot be copy-assigned.
        ScopedEnvironmentVariable(ScopedEnvironmentVariable &&) = delete;                 ///< Process-state ownership cannot be moved.
        ScopedEnvironmentVariable &operator=(ScopedEnvironmentVariable &&) = delete;      ///< Process-state ownership cannot be move-assigned.

        /// @brief Returns the construction status; a failed guard is inert.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::string name_;                         ///< Environment name owned by the guard.
        std::optional<std::string> previousValue_; ///< Captured prior value, or nullopt when absent.
        Types::InfrastructureStatus status_;       ///< Construction status.
    };

    /// @brief Temporarily unsets a process environment variable and restores its prior state on destruction.
    class ScopedUnsetEnvironmentVariable
    {
    public:
        /// @brief Attempts to unset name while retaining the previous process value.
        explicit ScopedUnsetEnvironmentVariable(std::string_view name) noexcept;
        /// @brief Best-effort restores the previous value or leaves a previously missing name unset.
        ~ScopedUnsetEnvironmentVariable() noexcept;

        ScopedUnsetEnvironmentVariable(const ScopedUnsetEnvironmentVariable &) = delete; ///< Process-state ownership cannot be copied.
        ScopedUnsetEnvironmentVariable &operator=(const ScopedUnsetEnvironmentVariable &) =
            delete;                                                                            ///< Process-state ownership cannot be copy-assigned.
        ScopedUnsetEnvironmentVariable(ScopedUnsetEnvironmentVariable &&) = delete;            ///< Process-state ownership cannot be moved.
        ScopedUnsetEnvironmentVariable &operator=(ScopedUnsetEnvironmentVariable &&) = delete; ///< Process-state ownership cannot be move-assigned.

        /// @brief Returns the construction status; a failed guard is inert.
        [[nodiscard]] Types::InfrastructureStatus status() const noexcept;

    private:
        std::string name_;                         ///< Environment name owned by the guard.
        std::optional<std::string> previousValue_; ///< Captured prior value, or nullopt when absent.
        Types::InfrastructureStatus status_;       ///< Construction status.
    };

    /// @brief Launches one process directly without a shell and waits for its process tree to finish or be terminated.
    /// @param options Launch path, UTF-8 arguments/environment, timeout, and byte-capture settings.
    /// @return Infrastructure status, child outcome, exact exit code when available, and optional raw output bytes.
    /// @note Child capture is arbitrary bytes. UTF-8 validation applies to process text passed into native text boundaries, not captured output.
    [[nodiscard]] Types::Process::Result runChildProcess(const Types::Process::Options &options) noexcept;
} // namespace GameWIP::TestSupport
