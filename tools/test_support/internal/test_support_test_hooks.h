/// @file test_support_test_hooks.h
/// @brief Source-tree-only deterministic failure injection for TestSupport validation.
/// @warning This header is not installed and must not be used by production consumers.

#pragma once

#include <cstdint>
#include <optional>

#ifndef TEST_SUPPORT_INTERNAL_TEST_HOOKS
#define TEST_SUPPORT_INTERNAL_TEST_HOOKS 0
#endif

#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
namespace GameWIP::TestSupport::TestHooks
{
    /// @brief Failure-injection points for the Win32 child-process backend.
    enum class ChildProcessFailurePoint : std::uint8_t
    {
        None,
        Allocation,
        Unsupported,
        Platform,
        ProcessSetup,
        HandleSetup,
        PipeCreation,
        ProcessLaunch,
        JobAssignment,
        CaptureSetup,
        ThreadCreation,
        ThreadResume,
        CaptureRead,
        Wait,
        ProcessInspection,
        ProcessCleanup
    };

    /// @brief Failure-injection points for filesystem helpers and guards.
    enum class FileFailurePoint : std::uint8_t
    {
        None,
        Read,
        Write,
        Exists,
        CreateDirectories,
        Remove,
        TemporaryDirectory,
        CurrentPath
    };

    /// @brief Failure-injection points for process environment guards.
    enum class EnvironmentFailurePoint : std::uint8_t
    {
        None,
        Read,
        Set,
        Unset
    };

    /// @brief Clears every pending one-shot failure injection.
    void reset() noexcept;

    /// @brief Forces the next child-process operation at point to fail with nativeCode.
    void forceNextChildProcessFailure(ChildProcessFailurePoint point, std::uint64_t nativeCode = 1) noexcept;

    /// @brief Forces the next file operation at point to fail with nativeCode.
    void forceNextFileFailure(FileFailurePoint point, std::uint64_t nativeCode = 1) noexcept;

    /// @brief Forces the next environment operation at point to fail with nativeCode.
    void forceNextEnvironmentFailure(EnvironmentFailurePoint point, std::uint64_t nativeCode = 1) noexcept;
} // namespace GameWIP::TestSupport::TestHooks

namespace GameWIP::TestSupport::Detail::TestHooks
{
    /// @brief Consumes one matching child-process failure injection.
    [[nodiscard]] std::optional<std::uint64_t> consumeChildProcessFailure(TestSupport::TestHooks::ChildProcessFailurePoint point) noexcept;

    /// @brief Consumes one matching file failure injection.
    [[nodiscard]] std::optional<std::uint64_t> consumeFileFailure(TestSupport::TestHooks::FileFailurePoint point) noexcept;

    /// @brief Consumes one matching environment failure injection.
    [[nodiscard]] std::optional<std::uint64_t> consumeEnvironmentFailure(TestSupport::TestHooks::EnvironmentFailurePoint point) noexcept;
} // namespace GameWIP::TestSupport::Detail::TestHooks
#endif
